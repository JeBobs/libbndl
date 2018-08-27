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

using namespace libbndl;

bool Bundle::Load(const std::string &name)
{
	std::ifstream m_stream;

	m_stream.open(name, std::ios::in | std::ios::binary | std::ios::ate);

	// Check if archive exists
	if (m_stream.fail())
		return false;

	Lock mutexLock(m_mutex);

	const auto fileSize = m_stream.tellg();
	m_stream.seekg(0, std::ios::beg);
	auto *buffer = new uint8_t[fileSize];
	m_stream.read(reinterpret_cast<char *>(buffer), fileSize);
	m_stream.close();
	auto reader = binaryio::BinaryReader(buffer);

	// Check if it's a BNDL archive
	auto magic = reader.ReadString(4);
	if (magic == std::string("bndl"))
		m_version = BNDL;
	else if (magic == std::string("bnd2"))
		m_version = BND2;
	else
		return false;

	// Only supporting BND2 atm.
	if (m_version == BNDL)
		return false;

	auto bundleVersion = reader.Read<uint32_t>();

	m_platform = reader.Read<Platform>();
	reader.SetBigEndian(m_platform != PC);

	if (reader.IsBigEndian())
		bundleVersion = (bundleVersion << 24) | (bundleVersion << 8 & 0xff0000) | (bundleVersion >> 8 & 0xff00) | (bundleVersion >> 24);
	// Little sanity check.
	if (bundleVersion != 2)
		return false;

	const auto headerLength = reader.Read<uint32_t>();
	// Another sanity check.
	if (headerLength != 48)
		return false;

	m_numEntries = reader.Read<uint32_t>();

	m_idBlockOffset = reader.Read<uint32_t>();
	m_fileBlockOffsets[0] = reader.Read<uint32_t>();
	m_fileBlockOffsets[1] = reader.Read<uint32_t>();
	m_fileBlockOffsets[2] = reader.Read<uint32_t>();

	m_flags = reader.Read<Flags>();

	// Last 8 bytes are padding.
	reader.Seek(8, std::ios::cur);


	m_entries.clear();
	if (m_flags & HasResourceStringTable)
	{
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
				m_entries[fileID] = e;
			}
		}
	}


	reader.Seek(m_idBlockOffset);

	for (auto i = 0U; i < m_numEntries; i++)
	{
		// These are stored in bundle as 64-bit (8-byte), but are really 32-bit.
		auto fileID = static_cast<uint32_t>(reader.Read<uint64_t>());
		assert(fileID != 0);
		Entry e = m_entries[fileID];
		e.info.checksum = static_cast<uint32_t>(reader.Read<uint64_t>());

		// The uncompressed sizes have a high nibble that varies depending on the file type for whatever reason.
		e.fileBlockData[0].uncompressedSize = reader.Read<uint32_t>();
		e.fileBlockData[1].uncompressedSize = reader.Read<uint32_t>();
		e.fileBlockData[2].uncompressedSize = reader.Read<uint32_t>();
		e.fileBlockData[0].compressedSize = reader.Read<uint32_t>();
		e.fileBlockData[1].compressedSize = reader.Read<uint32_t>();
		e.fileBlockData[2].compressedSize = reader.Read<uint32_t>();

		auto dataReader = reader.Copy();
		for (auto j = 0; j < 3; j++)
		{
			dataReader.Seek(m_fileBlockOffsets[j] + reader.Read<uint32_t>()); // Read offset

			auto &dataInfo = e.fileBlockData[j];

			const auto readSize = (m_flags & Compressed) ? dataInfo.compressedSize : (dataInfo.uncompressedSize & ~(0xFU << 28));
			if (readSize == 0)
			{
				dataInfo.data = nullptr;
				continue;
			}

			dataInfo.data = dataReader.Read<uint8_t *>(readSize);
		}

		e.info.pointersOffset = reader.Read<uint32_t>();
		e.info.fileType = reader.Read<FileType>();
		e.info.numberOfPointers = reader.Read<uint16_t>();

		reader.Seek(2, std::ios::cur); // Padding

		m_entries[fileID] = e;
	}

	return true;
}

void Bundle::Save(const std::string& name)
{
	assert(m_version == BND2);

	Lock mutexLock(m_mutex);

	auto writer = binaryio::BinaryWriter();

	writer.Write("bnd2", 4);
	writer.Write<uint32_t>(2); // Bundle version
	writer.Write(PC); // Only PC writing supported for now.
	writer.Write(48); // Header length

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

	writer.Seek(8, std::ios::cur); // padding


	// RESOURCE STRING TABLE
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
	auto entryDataPointerPos = std::vector<off_t[3]>(m_numEntries);
	auto entryIter = m_entries.begin();
	for (auto i = 0U; i < m_numEntries; i++)
	{
		writer.Write<uint64_t>(entryIter->first);

		Entry e = entryIter->second;

		writer.Write<uint64_t>(e.info.checksum);

		for (auto &dataInfo : e.fileBlockData)
			writer.Write(dataInfo.uncompressedSize);
		for (auto &dataInfo : e.fileBlockData)
			writer.Write(dataInfo.compressedSize);
		for (auto j = 0; j < 3; j++)
		{
			entryDataPointerPos[i][j] = writer.GetOffset();
			writer.Seek(4, std::ios::cur);
		}

		writer.Write(e.info.pointersOffset);
		writer.Write(e.info.fileType);
		writer.Write(e.info.numberOfPointers);

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
			Entry e = entryIter->second;

			const auto dataInfo = e.fileBlockData[i];
			const auto readSize = (m_flags & Compressed) ? dataInfo.compressedSize : (dataInfo.uncompressedSize & ~(0xFU << 28));

			if (readSize > 0)
			{
				writer.VisitAndWrite<uint32_t>(entryDataPointerPos[j][i], writer.GetOffset() - blockStart);
				writer.Write(dataInfo.data, readSize);
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

Bundle::EntryData* Bundle::GetBinary(const std::string &fileName)
{
	return GetBinary(HashFileName(fileName));
}

Bundle::EntryData* Bundle::GetBinary(uint32_t fileID)
{
	const auto it = m_entries.find(fileID);
	if (it == m_entries.end())
		return nullptr;

	const auto data = new EntryData;
	for (auto i = 0; i < 3; i++)
	{
		EntryDataBlock *dataBlock = GetBinary(fileID, i);
		data->fileBlockData[i] = *dataBlock;
		delete dataBlock;
	}

	data->pointersOffset = it->second.info.pointersOffset;
	data->numberOfPointers = it->second.info.numberOfPointers;

	return data;
}

Bundle::EntryDataBlock* Bundle::GetBinary(const std::string &fileName, uint32_t fileBlock)
{
	return GetBinary(HashFileName(fileName), fileBlock);
}

Bundle::EntryDataBlock* Bundle::GetBinary(uint32_t fileID, uint32_t fileBlock)
{
	const auto it = m_entries.find(fileID);
	if (it == m_entries.end())
		return nullptr;

	Lock mutexLock(m_mutex);

	const Entry e = it->second;

	const auto dataBlock = new EntryDataBlock;
	const auto dataInfo = e.fileBlockData[fileBlock];

	if (dataInfo.data == nullptr)
	{
		dataBlock->data = nullptr;
		dataBlock->size = 0;
		return dataBlock;
	}

	const auto buffer = dataInfo.data;
	const auto uncompressedSize = dataInfo.uncompressedSize & ~(0xFU << 28);

	auto *uncompressedBuffer = new uint8_t[uncompressedSize];

	if (m_flags & Compressed)
	{
		uLongf uncompressedSizeLong = uncompressedSize;
		const auto ret = uncompress(uncompressedBuffer, &uncompressedSizeLong, buffer, static_cast<uLong>(dataInfo.compressedSize));

		assert(ret == Z_OK);
		assert(uncompressedSize == uncompressedSizeLong);
	}
	else
	{
		std::memcpy(uncompressedBuffer, buffer, uncompressedSize);
	}

	dataBlock->data = uncompressedBuffer;
	dataBlock->size = uncompressedSize;

	return dataBlock;
}

Bundle::EntryInfo Bundle::GetInfo(const std::string &fileName) const
{
	return GetInfo(HashFileName(fileName));
}

Bundle::EntryInfo Bundle::GetInfo(uint32_t fileID) const
{
	const auto it = m_entries.find(fileID);
	if (it == m_entries.end())
		return {};
	
	return it->second.info;
}

bool Bundle::ReplaceEntry(const std::string &fileName, EntryData *data)
{
	return ReplaceEntry(HashFileName(fileName), data);
}

bool Bundle::ReplaceEntry(uint32_t fileID, EntryData *data)
{
	const auto it = m_entries.find(fileID);
	if (it == m_entries.end())
		return false;

	Lock mutexLock(m_mutex);

	Entry &e = it->second;

	for (auto i = 0; i < 3; i++)
	{
		const auto inDataInfo = data->fileBlockData[i];
		auto &outDataInfo = e.fileBlockData[i];

		if (inDataInfo.size == 0)
		{
			outDataInfo.data = nullptr;
			outDataInfo.uncompressedSize = 0;
			outDataInfo.compressedSize = 0;
			continue;
		}

		uint8_t *buffer;

		if (m_flags & Compressed)
		{
			const auto compBufferSize = compressBound(static_cast<uLong>(inDataInfo.size));
			const auto compBuffer = new uint8_t[compBufferSize];
			uLongf actualSize = compBufferSize;
			const auto ret = compress2(compBuffer, &actualSize, inDataInfo.data, static_cast<uLong>(inDataInfo.size), Z_BEST_COMPRESSION);

			if (ret != Z_OK)
			{
				delete[] compBuffer;
				assert(0);
				return false;
			}

			buffer = new uint8_t[actualSize];
			std::memcpy(buffer, compBuffer, actualSize);
			delete[] compBuffer;

			outDataInfo.compressedSize = actualSize;
		}
		else
		{
			buffer = new uint8_t[inDataInfo.size];
			std::memcpy(buffer, inDataInfo.data, inDataInfo.size);

			outDataInfo.compressedSize = 0;
		}

		const auto highNibble = outDataInfo.uncompressedSize & (0xFU << 28); // TODO: how is this calculated?
		outDataInfo.uncompressedSize = static_cast<uint32_t>(inDataInfo.size) | highNibble;
		outDataInfo.data = buffer;
	}

	e.info.pointersOffset = data->pointersOffset;
	e.info.numberOfPointers = data->numberOfPointers;

	return true;
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
