#include "MemoryTracker.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

namespace
{
	std::atomic<std::size_t> g_currentBytes{ 0 };
	std::atomic<std::size_t> g_peakBytes{ 0 };
	std::atomic<std::uint64_t> g_allocationCount{ 0 };

	/**
	 * Each block starts with a prefix that stores its size, so operator delete knows how much is
	 * freed. The prefix keeps the default new alignment, so the returned pointer stays aligned.
	 */
	constexpr std::size_t kPrefixSize = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
	static_assert(kPrefixSize >= sizeof(std::size_t));

	void RecordAllocation(std::size_t size) noexcept
	{
		const std::size_t current = g_currentBytes.fetch_add(size) + size;
		g_allocationCount.fetch_add(1);

		std::size_t peak = g_peakBytes.load();
		while (current > peak && !g_peakBytes.compare_exchange_weak(peak, current))
		{
		}
	}
}

// --- Replacement of the global allocation functions -----------------------------------------
// The array and nothrow forms of the standard library call these, so they are counted too.

void* operator new(std::size_t size)
{
	if (size > std::numeric_limits<std::size_t>::max() - kPrefixSize)
	{
		throw std::bad_alloc();
	}

	void* const block = std::malloc(size + kPrefixSize);
	if (block == nullptr)
	{
		throw std::bad_alloc();
	}

	*static_cast<std::size_t*>(block) = size;
	RecordAllocation(size);
	return static_cast<std::byte*>(block) + kPrefixSize;
}

void operator delete(void* pointer) noexcept
{
	if (pointer == nullptr)
	{
		return;
	}

	void* const block = static_cast<std::byte*>(pointer) - kPrefixSize;
	g_currentBytes.fetch_sub(*static_cast<const std::size_t*>(block));
	std::free(block);
}

void operator delete(void* pointer, std::size_t) noexcept
{
	operator delete(pointer);
}

void* operator new[](std::size_t size)
{
	return operator new(size);
}

void operator delete[](void* pointer) noexcept
{
	operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
	operator delete(pointer);
}

// --- Measurement API ------------------------------------------------------------------------

namespace app::memory
{
	std::size_t GetCurrentBytes() noexcept
	{
		return g_currentBytes.load();
	}

	Scope::Scope() noexcept
		: m_startBytes(g_currentBytes.load())
		, m_startAllocations(g_allocationCount.load())
	{
		g_peakBytes.store(m_startBytes);
	}

	std::size_t Scope::GetPeakBytes() const noexcept
	{
		const std::size_t peak = g_peakBytes.load();
		return peak > m_startBytes ? peak - m_startBytes : 0;
	}

	std::int64_t Scope::GetRetainedBytes() const noexcept
	{
		return static_cast<std::int64_t>(g_currentBytes.load()) - static_cast<std::int64_t>(m_startBytes);
	}

	std::uint64_t Scope::GetAllocationCount() const noexcept
	{
		return g_allocationCount.load() - m_startAllocations;
	}
}
