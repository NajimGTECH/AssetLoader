#include "pch.h"
#include "TestHelpers.h"

#include <random>
#include <string_view>

#include "Integrity/Crc32.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace IntegrityTests
{
	namespace
	{
		/** The bytes of an ASCII string (without the terminating zero). */
		std::span<const std::byte> AsBytes(std::string_view text)
		{
			return std::as_bytes(std::span<const char>(text.data(), text.size()));
		}

		/**
		 * Reference CRC-32 computed bit by bit, straight from the definition (no table).
		 * Independent of the implementation under test, to cross-check it on arbitrary data.
		 */
		std::uint32_t BitwiseCrc32(std::span<const std::byte> data)
		{
			std::uint32_t crc = 0xFFFFFFFFu;
			for (const std::byte byte : data)
			{
				crc ^= std::to_integer<std::uint32_t>(byte);
				for (int bit = 0; bit < 8; ++bit)
				{
					crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
				}
			}
			return ~crc;
		}

		/** Deterministic pseudo-random bytes (fixed seed, so failures are reproducible). */
		std::vector<std::byte> RandomBytes(std::size_t count, unsigned seed)
		{
			std::mt19937 generator(seed);
			std::uniform_int_distribution<unsigned> distribution(0, 255);

			std::vector<std::byte> bytes(count);
			for (std::byte& byte : bytes)
			{
				byte = static_cast<std::byte>(distribution(generator));
			}
			return bytes;
		}
	}

	/*
	* CRC-32 tests: published reference values, cross-check with a bitwise implementation,
	* incremental computation, and detection of small changes.
	*/
	TEST_CLASS(Crc32Tests)
	{
	public:

		TEST_METHOD(ReferenceValues)
		{
			// Standard check value of CRC-32 (ISO-HDLC), and other well-known results.
			Assert::AreEqual(0xCBF43926u, ComputeCrc32(AsBytes("123456789")));
			Assert::AreEqual(0x00000000u, ComputeCrc32(AsBytes("")));
			Assert::AreEqual(0xE8B7BE43u, ComputeCrc32(AsBytes("a")));
			Assert::AreEqual(0x414FA339u, ComputeCrc32(AsBytes("The quick brown fox jumps over the lazy dog")));
		}

		TEST_METHOD(MatchesBitwiseReference)
		{
			for (std::size_t length : { 1u, 2u, 3u, 7u, 8u, 31u, 64u, 255u, 256u, 1000u, 4096u })
			{
				const std::vector<std::byte> data = RandomBytes(length, static_cast<unsigned>(length));
				Assert::AreEqual(BitwiseCrc32(data), ComputeCrc32(data),
					(L"Mismatch for length " + std::to_wstring(length)).c_str());
			}

			const std::vector<std::byte> zeros(32, std::byte{ 0x00 });
			const std::vector<std::byte> ones(32, std::byte{ 0xFF });
			Assert::AreEqual(BitwiseCrc32(zeros), ComputeCrc32(zeros));
			Assert::AreEqual(BitwiseCrc32(ones), ComputeCrc32(ones));
		}

		TEST_METHOD(NewChecksumIsEmptyChecksum)
		{
			const Crc32 crc;
			Assert::AreEqual(0x00000000u, crc.GetValue());
		}

		TEST_METHOD(IncrementalEqualsOneShot)
		{
			const std::vector<std::byte> data = RandomBytes(200, 42);
			const std::span<const std::byte> bytes = data;
			const std::uint32_t expected = ComputeCrc32(data);

			// Every split point, including empty first / last pieces.
			for (std::size_t split = 0; split <= data.size(); ++split)
			{
				Crc32 crc;
				crc.Update(bytes.first(split));
				crc.Update(bytes.subspan(split));
				Assert::AreEqual(expected, crc.GetValue(), (L"Mismatch at split " + std::to_wstring(split)).c_str());
			}

			// Byte by byte.
			Crc32 crc;
			for (std::size_t i = 0; i < data.size(); ++i)
			{
				crc.Update(bytes.subspan(i, 1));
			}
			Assert::AreEqual(expected, crc.GetValue());
		}

		TEST_METHOD(GetValueDoesNotChangeTheState)
		{
			Crc32 crc;
			crc.Update(AsBytes("1234"));
			const std::uint32_t partial = crc.GetValue();
			Assert::AreEqual(partial, crc.GetValue());

			crc.Update(AsBytes("56789"));
			Assert::AreEqual(0xCBF43926u, crc.GetValue());
		}

		TEST_METHOD(ResetStartsAgain)
		{
			Crc32 crc;
			crc.Update(AsBytes("something else"));
			crc.Reset();
			Assert::AreEqual(0x00000000u, crc.GetValue());

			crc.Update(AsBytes("123456789"));
			Assert::AreEqual(0xCBF43926u, crc.GetValue());
		}

		TEST_METHOD(DetectsEverySingleBitError)
		{
			const std::vector<std::byte> data = RandomBytes(64, 7);
			const std::uint32_t original = ComputeCrc32(data);

			for (std::size_t index = 0; index < data.size(); ++index)
			{
				for (unsigned bit = 0; bit < 8; ++bit)
				{
					std::vector<std::byte> damaged = data;
					damaged[index] ^= static_cast<std::byte>(1u << bit);
					Assert::AreNotEqual(original, ComputeCrc32(damaged));
				}
			}
		}

		TEST_METHOD(DetectsSwappedBytes)
		{
			// A simple sum of the bytes would not see this change; a CRC does.
			const std::vector<std::byte> data = Bytes({ 1, 2, 3, 4 });
			const std::vector<std::byte> swapped = Bytes({ 2, 1, 3, 4 });
			Assert::AreNotEqual(ComputeCrc32(data), ComputeCrc32(swapped));
		}
	};
}
