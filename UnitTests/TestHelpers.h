#pragma once
#include "CppUnitTest.h"

#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

#include "ByteAccess/ReadResult.h"

/**
 * @file TestHelpers.h
 * @brief Shared helpers for the Byte Access Layer tests.
 */

// The test framework needs ToString() to print the values of failed Assert::AreEqual calls.
// Note: there is deliberately no specialization for asset::ReadError. The framework calls
// ToString(value) unqualified, so argument-dependent lookup would pick asset::ToString(ReadError)
// (which returns a std::string_view) and fail to compile. Read errors are compared through their
// message instead, see AssertOk / AssertFails below.
namespace Microsoft::VisualStudio::CppUnitTestFramework
{
	template <> inline std::wstring ToString<std::byte>(const std::byte& value)
	{
		wchar_t text[8]{};
		swprintf_s(text, L"0x%02X", std::to_integer<unsigned>(value));
		return text;
	}
}

namespace tests
{
	using Microsoft::VisualStudio::CppUnitTestFramework::Assert;

	/**
	 * @brief Builds a byte vector from integer literals, e.g. Bytes({ 0x44, 0x33, 0x22, 0x11 }).
	 */
	inline std::vector<std::byte> Bytes(std::initializer_list<unsigned> values)
	{
		std::vector<std::byte> bytes;
		bytes.reserve(values.size());
		for (const unsigned value : values)
		{
			bytes.push_back(static_cast<std::byte>(value));
		}
		return bytes;
	}

	/** @brief Asserts that two byte sequences have the same size and content, reporting the first mismatching index. */
	inline void AssertBytesEqual(std::span<const std::byte> expected, std::span<const std::byte> actual)
	{
		Assert::AreEqual(expected.size(), actual.size(), L"Byte count mismatch");
		for (std::size_t i = 0; i < expected.size(); ++i)
		{
			const std::wstring message = L"Byte mismatch at index " + std::to_wstring(i);
			Assert::AreEqual(expected[i], actual[i], message.c_str());
		}
	}

	/** @brief Asserts that a read failed with the expected error (compared by message, so failures are readable). */
	inline void AssertFails(asset::ReadError expected, asset::ReadResult result, const wchar_t* message = nullptr)
	{
		Assert::AreEqual(asset::ToString(expected), asset::ToString(result.GetError()), message);
		Assert::IsTrue(result == expected, message);
	}

	/** @brief Asserts that a read succeeded. */
	inline void AssertOk(asset::ReadResult result, const wchar_t* message = nullptr)
	{
		Assert::AreEqual(asset::ToString(asset::ReadError::None), asset::ToString(result.GetError()), message);
		Assert::IsTrue(result.IsOk(), message);
	}
}
