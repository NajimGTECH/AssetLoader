#pragma once
#include <cstddef>
#include <cstdint>

/**
 * @file MemoryTracker.h
 * @brief Measures heap memory used by an operation (benchmark only).
 *
 * MemoryTracker.cpp replaces the global operator new / operator delete of the demo executable:
 * every allocation made with `new` (std::vector, std::map, std::string, file streams...) goes
 * through a counter of bytes currently allocated, which also records its highest value.
 *
 * Limitations: only C++ heap allocations are counted. Memory allocated with malloc by C code, by
 * the operating system (file cache, memory-mapped files) or on the stack is not; the allocator's
 * own overhead per block is not either.
 */
namespace app::memory
{
	/** @brief Bytes currently allocated with operator new. */
	[[nodiscard]] std::size_t GetCurrentBytes() noexcept;

	/**
	 * @class Scope
	 * @brief Measures the heap usage between its construction and the queries.
	 *
	 * Scopes must not be nested: each one resets the recorded peak.
	 */
	class Scope
	{
	public:
		Scope() noexcept;

		/** @brief Highest number of bytes allocated at the same time above the starting point. */
		[[nodiscard]] std::size_t GetPeakBytes() const noexcept;

		/** @brief Bytes still allocated now compared to the starting point (negative if freed). */
		[[nodiscard]] std::int64_t GetRetainedBytes() const noexcept;

		/** @brief Number of calls to operator new since the starting point. */
		[[nodiscard]] std::uint64_t GetAllocationCount() const noexcept;

	private:
		std::size_t m_startBytes;
		std::uint64_t m_startAllocations;
	};
}
