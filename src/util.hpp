#pragma once
#include <fstream>
#include <cstdint>

namespace libbndl
{
	template <typename T, typename = typename std::is_enum<T>::type>
	struct safe_underlying_type
	{
		using type = T;
	};

	template <typename T>
	struct safe_underlying_type<T, std::true_type>
	{
		using type = std::underlying_type_t<T>;
	};

	inline uint8_t endianSwap(uint8_t v)
	{
		return v;
	}

	inline uint16_t endianSwap(uint16_t v)
	{
		return (v << 8) | (v >> 8);
	}

	inline uint32_t endianSwap(uint32_t v)
	{
		return (v << 24) | (v << 8 & 0xff0000) | (v >> 8 & 0xff00) | (v >> 24);
	}

	inline uint64_t endianSwap(uint64_t v)
	{
		return (v << 56) |
			((v << 40) & 0xff000000000000) |
			((v << 24) & 0xff0000000000) |
			((v << 8) & 0xff00000000) |
			((v >> 8) & 0xff000000) |
			((v >> 24) & 0xff0000) |
			((v >> 40) & 0xff00) |
			(v >> 56);
	}

	template <typename T, typename = typename std::enable_if<std::is_unsigned<typename safe_underlying_type<T>::type>::value>::type>
	T read(std::fstream& stream, bool reverse = false)
	{
		T result;
		stream.read(reinterpret_cast<char*>(&result), sizeof(T));
		if (reverse)
			result = static_cast<T>(endianSwap(result));
		return result;
	}
}
