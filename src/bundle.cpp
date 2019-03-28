#include <libbndl/bundle.hpp>
#include "lock.hpp"
#include <binaryio/binaryreader.hpp>
#include <binaryio/binarywriter.hpp>
#include <fstream>
#include <cassert>
#include <zlib.h>
#include <pugixml.hpp>
#include <regex>
#include <iomanip>
#include <array>

using namespace libbndl;

#ifndef __has_builtin
#	define __has_builtin(x) 0
#endif
inline unsigned long BitScanReverse(unsigned long input)
{
	unsigned long result;

#if defined(_MSC_VER)
	_BitScanReverse(&result, input);
#elif __has_builtin(__builtin_clzl) || defined(__GNUC__)
	result = static_cast<unsigned long>(31 - __builtin_clzl(input));
#else
#	error "Unsupported compiler."
#endif

	return result;
}

bool Bundle::Load(const std::string &name)
{
	std::ifstream stream;

	stream.open(name, std::ios::in | std::ios::binary | std::ios::ate);

	// Check if archive exists
	if (stream.fail())
		return false;

	Lock mutexLock(m_mutex);

	const auto fileSize = stream.tellg();
	stream.seekg(0, std::ios::beg);
	const auto &buffer = std::make_shared<std::vector<uint8_t>>(fileSize);
	stream.read(reinterpret_cast<char *>(buffer->data()), fileSize);
	stream.close();
	auto reader = binaryio::BinaryReader(buffer);

	// Check if it's a BNDL archive
	auto magic = reader.ReadString(4);
	if (magic == std::string("bndl"))
		m_magicVersion = BNDL;
	else if (magic == std::string("bnd2"))
		m_magicVersion = BND2;
	else
		return false;

	return (m_magicVersion == BNDL) ? LoadBNDL(reader): LoadBND2(reader);
}

bool Bundle::LoadBND2(binaryio::BinaryReader &reader)
{
	m_revisionNumber = reader.Read<uint32_t>();

	m_platform = reader.Read<Platform>();
	reader.SetBigEndian(m_platform != PC);

	if (reader.IsBigEndian())
		m_revisionNumber = (m_revisionNumber << 24) | (m_revisionNumber << 8 & 0xff0000) | (m_revisionNumber >> 8 & 0xff00) | (m_revisionNumber >> 24);
	// Little sanity check.
	if (m_revisionNumber != 2)
		return false;

	const auto rstOffset = reader.Read<uint32_t>();

	m_numEntries = reader.Read<uint32_t>();

	const auto idBlockOffset = reader.Read<uint32_t>();
	m_fileBlockOffsets[0] = reader.Read<uint32_t>();
	m_fileBlockOffsets[1] = reader.Read<uint32_t>();
	m_fileBlockOffsets[2] = reader.Read<uint32_t>();

	m_flags = reader.Read<Flags>();

	// Last 8 bytes are padding.


	m_entries.clear();
	if (m_flags & HasResourceStringTable)
	{
		reader.Seek(rstOffset, std::ios::beg);

		const auto rstXML = reader.ReadString();

		pugi::xml_document doc;
		if (doc.load_string(rstXML.c_str(), pugi::parse_minimal))
		{
			for (const auto resource : doc.child("ResourceStringTable").children("Resource"))
			{
				const auto fileID = std::stoul(resource.attribute("id").value(), nullptr, 16);
				Entry e = {};
				e.info.name = resource.attribute("name").value();
				e.info.typeName = resource.attribute("type").value();
				m_entries[fileID] = std::move(e);
			}
		}
	}


	reader.Seek(idBlockOffset);

	for (auto i = 0U; i < m_numEntries; i++)
	{
		// These are stored in bundle as 64-bit (8-byte), but are really 32-bit.
		auto fileID = static_cast<uint32_t>(reader.Read<uint64_t>());
		assert(fileID != 0);
		auto &e = m_entries[fileID];
		e.info.checksum = static_cast<uint32_t>(reader.Read<uint64_t>());

		// The uncompressed sizes have a high nibble that varies depending on the file type.
		const auto uncompSize0 = reader.Read<uint32_t>();
		e.fileBlockData[0].uncompressedSize = uncompSize0 & ~(0xFU << 28);
		e.fileBlockData[0].uncompressedAlignment = 1 << (uncompSize0 >> 28);
		const auto uncompSize1 = reader.Read<uint32_t>();
		e.fileBlockData[1].uncompressedSize = uncompSize1 & ~(0xFU << 28);
		e.fileBlockData[1].uncompressedAlignment = 1 << (uncompSize1 >> 28);
		const auto uncompSize2 = reader.Read<uint32_t>();
		e.fileBlockData[2].uncompressedSize = uncompSize2 & ~(0xFU << 28);
		e.fileBlockData[2].uncompressedAlignment = 1 << (uncompSize2 >> 28);

		e.fileBlockData[0].compressedSize = reader.Read<uint32_t>();
		e.fileBlockData[1].compressedSize = reader.Read<uint32_t>();
		e.fileBlockData[2].compressedSize = reader.Read<uint32_t>();

		auto dataReader = reader.Copy();
		for (auto j = 0; j < 3; j++)
		{
			dataReader.Seek(m_fileBlockOffsets[j] + reader.Read<uint32_t>()); // Read offset

			auto &dataInfo = e.fileBlockData[j];

			const auto readSize = (m_flags & Compressed) ? dataInfo.compressedSize : dataInfo.uncompressedSize;
			if (readSize == 0)
			{
				dataInfo.data = nullptr;
				continue;
			}

			const auto readBuffer = dataReader.Read<uint8_t *>(readSize);
			dataInfo.data = std::make_unique<std::vector<uint8_t>>(readBuffer, readBuffer + readSize);
		}

		e.info.dependenciesOffset = reader.Read<uint32_t>();
		e.info.fileType = reader.Read<FileType>();
		e.info.numberOfDependencies = reader.Read<uint16_t>();

		reader.Seek(2, std::ios::cur); // Padding
	}

	return true;
}

bool Bundle::LoadBNDL(binaryio::BinaryReader &reader)
{
	reader.SetBigEndian(true); // Never released on PC.

	// A lot of this is unknown.
	m_revisionNumber = reader.Read<uint32_t>(); // probably
	if (m_revisionNumber != 5)
		return false;
	m_numEntries = reader.Read<uint32_t>();
	const auto block2Offset = reader.Read<uint32_t>();
	reader.Seek(0x38, std::ios::cur);
	const auto idListOffset = reader.Read<uint32_t>();
	const auto idTableOffset = reader.Read<uint32_t>();
	reader.Skip<uint32_t>(); // dependency block
	reader.Seek(4, std::ios::cur);
	m_platform = Xbox360; // Xbox only for now.
	reader.Skip<uint32_t>();//m_platform = reader.Read<Platform>(); // maybe
	const auto compressed = reader.Read<uint32_t>();
	if (compressed)
		m_flags = Compressed; // TODO
	else
		m_flags = static_cast<Flags>(0);
	reader.Skip<uint32_t>(); // unknown purpose: sometimes repeats m_numEntries
	const auto uncompInfoOffset = reader.Read<uint32_t>();

	m_entries.clear();

	reader.Seek(idListOffset);
	std::vector<uint32_t> fileIDs;
	for (auto i = 0U; i < m_numEntries; i++)
		fileIDs.push_back(static_cast<uint32_t>(reader.Read<uint64_t>()));

	reader.Seek(idTableOffset);
	for (const auto fileID : fileIDs)
	{
		auto &e = m_entries[fileID];

		reader.Skip<uint32_t>(); // unknown mem stuff
		e.info.dependenciesOffset = reader.Read<uint32_t>();
		e.info.fileType = reader.Read<FileType>();

		if (compressed)
		{
			e.fileBlockData[0].compressedSize = reader.Read<uint32_t>();
			reader.Skip<uint32_t>(); // Alignment value, should be 1
			e.fileBlockData[1].compressedSize = reader.Read<uint32_t>();
			reader.Skip<uint32_t>(); // Alignment value, should be 1
			e.fileBlockData[2].compressedSize = reader.Read<uint32_t>();
			reader.Skip<uint32_t>(); // Alignment value, should be 1
			reader.Skip<uint32_t>(); // other blocks. Maybe used but I'm ignoring it.
			reader.Skip<uint32_t>(); // Alignment value, should be 1
			reader.Skip<uint32_t>(); // other blocks. Maybe used but I'm ignoring it.
			reader.Skip<uint32_t>(); // Alignment value, should be 1
		}
		else
		{
			e.fileBlockData[0].uncompressedSize = reader.Read<uint32_t>();
			e.fileBlockData[0].uncompressedAlignment = reader.Read<uint32_t>();
			e.fileBlockData[1].uncompressedSize = reader.Read<uint32_t>();
			e.fileBlockData[1].uncompressedAlignment = reader.Read<uint32_t>();
			e.fileBlockData[2].uncompressedSize = reader.Read<uint32_t>();
			e.fileBlockData[2].uncompressedAlignment = reader.Read<uint32_t>();
			reader.Skip<uint32_t>(); // other blocks. Maybe used but I'm ignoring it.
			reader.Skip<uint32_t>(); // Alignment value
			reader.Skip<uint32_t>(); // other blocks. Maybe used but I'm ignoring it.
			reader.Skip<uint32_t>(); // Alignment value
		}

		auto dataReader = reader.Copy();
		for (auto j = 0; j < 5; j++)
		{
			auto readOffset = reader.Read<uint32_t>();
			reader.Skip<uint32_t>(); // 1

			if (j > 2)
				continue; // Not supporting blocks 4 and 5 right now.

			auto &dataInfo = e.fileBlockData[j];

			const auto readSize = compressed ? dataInfo.compressedSize : dataInfo.uncompressedSize;
			if (readSize == 0)
			{
				dataInfo.data = nullptr;
				continue;
			}

			if (j > 0 && readOffset == 0)
				readOffset = block2Offset; // idk
			dataReader.Seek(readOffset); // Read offset

			const auto readBuffer = dataReader.Read<uint8_t *>(readSize);
			dataInfo.data = std::make_unique<std::vector<uint8_t>>(readBuffer, readBuffer + readSize);
		}

		reader.Seek(0x14, std::ios::cur); // Unknown mem stuff
	}

	if (compressed)
	{
		reader.Seek(uncompInfoOffset);
		for (const auto fileID : fileIDs)
		{
			auto &e = m_entries[fileID];

			e.fileBlockData[0].uncompressedSize = reader.Read<uint32_t>();
			e.fileBlockData[0].uncompressedAlignment = reader.Read<uint32_t>();
			e.fileBlockData[1].uncompressedSize = reader.Read<uint32_t>();
			e.fileBlockData[1].uncompressedAlignment = reader.Read<uint32_t>();
			e.fileBlockData[2].uncompressedSize = reader.Read<uint32_t>();
			e.fileBlockData[2].uncompressedAlignment = reader.Read<uint32_t>();
			reader.Skip<uint32_t>(); // other blocks. Maybe used but I'm ignoring it.
			reader.Skip<uint32_t>(); // Alignment value
			reader.Skip<uint32_t>(); // other blocks. Maybe used but I'm ignoring it.
			reader.Skip<uint32_t>(); // Alignment value
		}
	}

	for (const auto fileID : fileIDs)
	{
		auto &e = m_entries[fileID];
		const auto depOffset = e.info.dependenciesOffset;
		if (depOffset == 0)
			continue;

		reader.Seek(depOffset);
		e.info.numberOfDependencies = static_cast<uint16_t>(reader.Read<uint32_t>());
		reader.Skip<uint32_t>();
		for (auto i = 0U; i < e.info.numberOfDependencies; i++)
			m_dependencies[fileID].emplace_back(ReadDependency(reader));
	}

	auto rstFile = GetBinary(0xC039284A, 0);
	if (rstFile == nullptr)
		return true;

	auto rstReader = binaryio::BinaryReader(std::move(rstFile));

	const auto strLen = rstReader.Read<uint32_t>();
	const auto rstXML = rstReader.ReadString(strLen);

	pugi::xml_document doc;
	if (doc.load_string(rstXML.c_str(), pugi::parse_minimal))
	{
		for (const auto resource : doc.child("ResourceStringTable").children("Resource"))
		{
			const auto fileID = std::stoul(resource.attribute("id").value(), nullptr, 16);
			auto &e = m_entries[fileID];
			e.info.name = resource.attribute("name").value();
			e.info.typeName = resource.attribute("type").value();
		}
	}

	return true;
}

void Bundle::Save(const std::string& name)
{
	assert(m_magicVersion == BND2);

	Lock mutexLock(m_mutex);

	auto writer = binaryio::BinaryWriter();

	writer.Write("bnd2", 4);
	writer.Write<uint32_t>(2); // Bundle version
	writer.Write(PC); // Only PC writing supported for now.

	auto rstPointerPos = writer.GetOffset();
	writer.Seek(4, std::ios::cur); // write later

	writer.Write(m_numEntries);

	auto idBlockPointerPos = writer.GetOffset();
	writer.Seek(4, std::ios::cur); // write later
	off_t fileBlockPointerPos[3];
	for (auto &pointerPos : fileBlockPointerPos)
	{
		pointerPos = writer.GetOffset();
		writer.Seek(4, std::ios::cur);
	}

	writer.Write(m_flags);

	writer.Align(16);


	// RESOURCE STRING TABLE
	writer.VisitAndWrite<uint32_t>(rstPointerPos, writer.GetOffset());
	if (m_flags & HasResourceStringTable)
	{
		pugi::xml_document doc;
		auto root = doc.append_child("ResourceStringTable");
		for (const auto &entry : m_entries)
		{
			auto entryChild = root.append_child("Resource");

			std::stringstream idStream;
			idStream << std::hex << std::setw(8) << std::setfill('0') << entry.first;

			entryChild.append_attribute("id").set_value(idStream.str().c_str());
			entryChild.append_attribute("type").set_value(entry.second.info.typeName.c_str());
			entryChild.append_attribute("name").set_value(entry.second.info.name.c_str());
		}

		std::stringstream out;
		doc.save(out, "\t", pugi::format_indent | pugi::format_no_declaration, pugi::encoding_utf8);
		const auto outStr = std::regex_replace(out.str(), std::regex(" />\n"), "/>\n");
		writer.Write(outStr);

		writer.Align(16);
	}


	// ID BLOCK
	writer.VisitAndWrite<uint32_t>(idBlockPointerPos, writer.GetOffset());
	auto entryDataPointerPos = std::vector<std::array<off_t, 3>>(m_numEntries);
	auto entryIter = m_entries.begin();
	for (auto i = 0U; i < m_numEntries; i++)
	{
		writer.Write<uint64_t>(entryIter->first);

		const auto &e = entryIter->second;

		writer.Write<uint64_t>(e.info.checksum);

		for (auto &dataInfo : e.fileBlockData)
			writer.Write(dataInfo.uncompressedSize | (BitScanReverse(dataInfo.uncompressedAlignment) << 28));
		for (auto &dataInfo : e.fileBlockData)
			writer.Write(dataInfo.compressedSize);
		for (auto j = 0; j < 3; j++)
		{
			entryDataPointerPos[i][j] = writer.GetOffset();
			writer.Seek(4, std::ios::cur);
		}

		writer.Write(e.info.dependenciesOffset);
		writer.Write(e.info.fileType);
		writer.Write(e.info.numberOfDependencies);

		writer.Seek(2, std::ios::cur); // padding

		entryIter = std::next(entryIter);
	}

	// DATA BLOCK
	for (auto i = 0; i < 3; i++)
	{
		const auto blockStart = writer.GetOffset();
		writer.VisitAndWrite<uint32_t>(fileBlockPointerPos[i], blockStart);

		entryIter = m_entries.begin();
		for (auto j = 0U; j < m_numEntries; j++)
		{
			const auto &e = entryIter->second;

			const auto &dataInfo = e.fileBlockData[i];
			const auto readSize = (m_flags & Compressed) ? dataInfo.compressedSize : dataInfo.uncompressedSize;

			if (readSize > 0)
			{
				writer.VisitAndWrite<uint32_t>(entryDataPointerPos[j][i], writer.GetOffset() - blockStart);
				writer.Write(dataInfo.data->data(), readSize);
				writer.Align((i != 0 && j != m_numEntries - 1) ? 0x80 : 16);
			}

			entryIter = std::next(entryIter);
		}

		if (i != 2)
			writer.Align(0x80);
	}

	std::ofstream f(name, std::ios::out | std::ios::binary);
	f << writer.GetStream().rdbuf();
	f.close();
}

uint32_t Bundle::HashFileName(std::string fileName) const
{
	std::transform(fileName.begin(), fileName.end(), fileName.begin(), tolower);
	return crc32_z(0, reinterpret_cast<const Bytef *>(fileName.c_str()), fileName.length());
}

Bundle::Dependency Bundle::ReadDependency(binaryio::BinaryReader &reader)
{
	const Dependency &dep = {
		static_cast<uint32_t>(reader.Read<uint64_t>()),
		reader.Read<uint32_t>()
	};
	reader.Skip<uint32_t>();
	return dep;
}

std::optional<Bundle::EntryData> Bundle::GetData(const std::string &fileName) const
{
	return GetData(HashFileName(fileName));
}

std::optional<Bundle::EntryData> Bundle::GetData(uint32_t fileID) const
{
	const auto it = m_entries.find(fileID);
	if (it == m_entries.end())
		return {};

	EntryData data;
	for (auto i = 0; i < 3; i++)
		data.fileBlockData[i] = GetBinary(fileID, i);

	const auto numDependencies = it->second.info.numberOfDependencies;
	if (numDependencies > 0)
	{
		if (m_magicVersion == BNDL)
		{
			data.dependencies = m_dependencies.at(fileID);
		}
		else
		{
			const auto buffer = std::make_shared<std::vector<uint8_t>>(data.fileBlockData[0]->begin() + it->second.info.dependenciesOffset, data.fileBlockData[0]->end());
			binaryio::BinaryReader reader(buffer, m_platform != PC);
			for (auto i = 0U; i < numDependencies; i++)
				data.dependencies.emplace_back(ReadDependency(reader));
			data.fileBlockData[0]->resize(data.fileBlockData[0]->size() - buffer->size());
		}
	}

	return data;
}

std::unique_ptr<std::vector<uint8_t>> Bundle::GetBinary(const std::string &fileName, uint32_t fileBlock) const
{
	return GetBinary(HashFileName(fileName), fileBlock);
}

std::unique_ptr<std::vector<uint8_t>> Bundle::GetBinary(uint32_t fileID, uint32_t fileBlock) const
{
	const auto it = m_entries.find(fileID);
	if (it == m_entries.end())
		return {};

	const auto &e = it->second;

	const auto &dataInfo = e.fileBlockData[fileBlock];

	if (dataInfo.data == nullptr)
		return {};

	const auto &buffer = dataInfo.data;
	const auto uncompressedSize = dataInfo.uncompressedSize;

	auto uncompressedBuffer = std::make_unique<std::vector<uint8_t>>(uncompressedSize);

	if (m_flags & Compressed)
	{
		uLongf uncompressedSizeLong = uncompressedSize;
		const auto ret = uncompress(uncompressedBuffer->data(), &uncompressedSizeLong, buffer->data(), static_cast<uLong>(dataInfo.compressedSize));

		assert(ret == Z_OK);
		assert(uncompressedSize == uncompressedSizeLong);
	}
	else
	{
		std::memcpy(uncompressedBuffer->data(), buffer->data(), uncompressedSize);
	}

	return std::move(uncompressedBuffer);
}

std::optional<Bundle::EntryInfo> Bundle::GetInfo(const std::string &fileName) const
{
	return GetInfo(HashFileName(fileName));
}

std::optional<Bundle::EntryInfo> Bundle::GetInfo(uint32_t fileID) const
{
	const auto it = m_entries.find(fileID);
	if (it == m_entries.end())
		return {};
	
	return it->second.info;
}

bool Bundle::ReplaceEntry(const std::string &fileName, const EntryData &data)
{
	return ReplaceEntry(HashFileName(fileName), data);
}

bool Bundle::ReplaceEntry(uint32_t fileID, const EntryData &data)
{
	const auto it = m_entries.find(fileID);
	if (it == m_entries.end() || data.dependencies.size() > std::numeric_limits<uint16_t>::max())
		return false;

	Lock mutexLock(m_mutex);

	Entry &e = it->second;

	e.info.dependenciesOffset = 0;
	e.info.numberOfDependencies = 0;

	for (auto i = 0; i < 3; i++)
	{
		const auto &inDataInfo = data.fileBlockData[i];
		auto &outDataInfo = e.fileBlockData[i];

		if (inDataInfo == nullptr || inDataInfo->empty())
		{
			outDataInfo.data = nullptr;
			outDataInfo.uncompressedSize = 0;
			outDataInfo.compressedSize = 0;
			continue;
		}

		std::unique_ptr<std::vector<uint8_t>> inBuffer;
		std::unique_ptr<std::vector<uint8_t>> outBuffer;

		if (i == 0 && data.dependencies.size() > 0)
		{
			binaryio::BinaryWriter writer;
			for (const auto &dependency : data.dependencies)
				WriteDependency(writer, dependency);
			const auto depSize = writer.GetSize();
			auto depStream = writer.GetStream();

			const auto inSize = inDataInfo->size();
			binaryio::Align(inSize, 16);
			inBuffer = std::make_unique<std::vector<uint8_t>>(inSize + depSize);
			inBuffer->assign(inDataInfo->begin(), inDataInfo->end());
			inBuffer->resize(inSize);
			inBuffer->insert(inBuffer->end(), std::istreambuf_iterator<char>(depStream), std::istreambuf_iterator<char>());

			e.info.dependenciesOffset = static_cast<uint32_t>(inSize);
			e.info.numberOfDependencies = static_cast<uint16_t>(data.dependencies.size());
		}
		else
		{
			inBuffer = std::make_unique<std::vector<uint8_t>>(inDataInfo->begin(), inDataInfo->end());
		}

		if (m_flags & Compressed)
		{
			const auto compBufferSize = compressBound(static_cast<uLong>(inBuffer->size()));
			outBuffer = std::make_unique<std::vector<uint8_t>>(compBufferSize);
			uLongf actualSize = compBufferSize;
			const auto ret = compress2(outBuffer->data(), &actualSize, inBuffer->data(), static_cast<uLong>(inBuffer->size()), Z_BEST_COMPRESSION);

			if (ret != Z_OK)
			{
				assert(0);
				return false;
			}

			outBuffer->shrink_to_fit();
			outDataInfo.compressedSize = actualSize;
		}
		else
		{
			outBuffer = std::move(inBuffer);
			outDataInfo.compressedSize = 0;
		}

		outDataInfo.uncompressedSize = static_cast<uint32_t>(inBuffer->size());
		outDataInfo.data = std::move(outBuffer);
	}

	return true;
}

void Bundle::WriteDependency(binaryio::BinaryWriter &writer, const Dependency &dependency)
{
	writer.Write<uint64_t>(dependency.fileID);
	writer.Write(dependency.internalOffset);
	writer.Align(8);
}

std::vector<uint32_t> Bundle::ListFileIDs() const
{
	std::vector<uint32_t> entries;
	for (const auto &e : m_entries)
	{
		entries.push_back(e.first);
	}
	return entries;
}

std::map<Bundle::FileType, std::vector<uint32_t>> Bundle::ListFileIDsByFileType() const
{
	std::map<FileType, std::vector<uint32_t>> entriesByFileType;
	for (const auto &e : m_entries)
	{
		entriesByFileType[e.second.info.fileType].push_back(e.first);
	}
	return entriesByFileType;
}
