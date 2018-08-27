#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>

namespace libbndl
{
	class Bundle
	{
	public:
		enum Version
		{
			BNDL	= 1,
			BND2	= 2
		};

		enum Platform: uint32_t
		{
			PC = 1, // (or PS4/XB1)
			XBOX = 2 << 24, // Big endian
			PS3 = 3 << 24, // Big endian
		};

		enum Flags: uint32_t
		{
			Compressed = 1,
			UnknownFlag1 = 2, // Always set?
			UnknownFlag2 = 4, // Always set?
			HasResourceStringTable = 8
			// There may be more but they are always 0 it seems.
		};

		enum FileType: uint32_t
		{
			Raster = 0x00,
			Material = 0x01,
			TextFile = 0x03,
			VertexDesc = 0x0A,
			Type0B = 0x0B,
			Renderable = 0x0C,
			Type0D = 0x0D,
			TextureState = 0x0E,
			MaterialState = 0x0F,
			ShaderProgramBuffer = 0x12,
			ShaderParameter = 0x14,
			Debug = 0x16,
			KdTree = 0x17,
			Snr = 0x19,
			AttribSysSchema = 0x1B,
			AttribSysVault = 0x1C,
			AptDataHeaderType = 0x1E,
			GuiPopup = 0x1F,
			Font = 0x21,
			LuaCode = 0x22,
			InstanceList = 0x23,
			CollisionMeshData = 0x24,
			IDList = 0x25,
			Language = 0x27,
			SatNavTile = 0x28,
			SatNavTileDirectory = 0x29,
			Model = 0x2A,
			RwColourCube = 0x2B,
			HudMessage = 0x2C,
			HudMessageList = 0x2D,
			HudMessageSequence = 0x2E,
			HudMessageSequenceDictionary = 0x2F,
			WorldPainter2D = 0x30,
			PFXHookBundle = 0x31,
			Shader = 0x32,
			ICETakeDictionary = 0x41,
			VideoData = 0x42,
			PolygonSoupList = 0x43,
			CommsToolListDefinition = 0x45,
			CommsToolList = 0x46,
			AnimationCollection = 0x51,
			Registry = 0xA000,
			GenericRwacWaveContent = 0xA020,
			GinsuWaveContent = 0xA021,
			AemsBank = 0xA022,
			Csis = 0xA023,
			Nicotine = 0xA024,
			Splicer = 0xA025,
			GenericRwacReverbIRContent = 0xA028,
			SnapshotData = 0xA029,
			ZoneList = 0xB000,
			LoopModel = 0x10000,
			AISections = 0x10001,
			TrafficData = 0x10002,
			Trigger = 0x10003,
			DeformationModel = 0x10004,
			VehicleList = 0x10005,
			GraphicsSpec = 0x10006,
			ParticleDescriptionCollection = 0x10008,
			WheelList = 0x10009,
			WheelGraphicsSpec = 0x1000A,
			TextureNameMap = 0x1000B,
			ICEList = 0x1000C,
			ICEData = 0x1000D,
			Progression = 0x1000E,
			PropPhysics = 0x1000F,
			PropGraphicsList = 0x10010,
			PropInstanceData = 0x10011,
			BrnEnvironmentKeyframe = 0x10012,
			BrnEnvironmentTimeLine = 0x10013,
			BrnEnvironmentDictionary = 0x10014,
			GraphicsStub = 0x10015,
			StaticSoundMap = 0x10016,
			StreetData = 0x10018,
			BrnVFXMeshCollection = 0x10019,
			MassiveLookupTable = 0x1001A,
			VFXPropCollection = 0x1001B,
			StreamedDeformationSpec = 0x1001C,
			ParticleDescription = 0x1001D,
			PlayerCarColours = 0x1001E,
			ChallengeList = 0x1001F,
			FlaptFile = 0x10020,
			ProfileUpgrade = 0x10021,
			VehicleAnimation = 0x10023,
			BodypartRemapping = 0x10024,
			LUAList = 0x10025,
			LUAScript = 0x10026
		};


		struct EntryFileBlockData
		{
			uint32_t uncompressedSize;
			uint32_t compressedSize;
			uint8_t *data;
		};

		struct EntryInfo
		{
			// In ResourceStringTable
			std::string name;
			std::string typeName;

			uint32_t checksum; // Stored in bundle as 64-bit (8-byte)

			uint32_t pointersOffset;
			FileType fileType;
			uint16_t numberOfPointers;
		};

		struct Entry
		{
			EntryInfo info;
			struct EntryFileBlockData fileBlockData[3];
		};


		struct EntryDataBlock
		{
			size_t size;
			uint8_t *data;
		};

		struct EntryData
		{
			EntryDataBlock fileBlockData[3];
			uint32_t pointersOffset;
			uint16_t numberOfPointers;
		};


		bool Load(const std::string &name);
		void Save(const std::string &name);

		Version GetVersion() const
		{
			return m_version;
		}

		Platform GetPlatform() const
		{
			return m_platform;
		}

		EntryInfo GetInfo(uint32_t fileID) const;
		EntryData* GetBinary(uint32_t fileID);
		EntryDataBlock* GetBinary(uint32_t fileID, uint32_t fileBlock);

		// Add Entry coming soon
		//void ReplaceEntry(uint32_t fileID, EntryData *data);

		std::vector<uint32_t> ListEntries() const;
		std::map<FileType, std::vector<uint32_t>> ListEntriesByFileType() const;

	private:
		std::mutex					m_mutex;
		std::map<uint32_t, Entry>	m_entries;

		Version						m_version;
		Platform					m_platform;
		uint32_t					m_numEntries;
		uint32_t					m_idBlockOffset;
		uint32_t					m_fileBlockOffsets[3];
		Flags						m_flags;
	};
}