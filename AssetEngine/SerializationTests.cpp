
#include <cstdint>
#include <iostream>
#include <vector>

#include "include/ByteAccess/ByteReader.h"
#include "include/ByteAccess/ByteWriter.h"
#include "include/ByteAccess/ReadResult.h"
#include "Deserializer.h"
#include "Serializer.h"
#include "SerializationTraits.h"


// Runs all serialization tests.
bool RunSerializationTests()
{
    bool allTestsPassed = true;

    // Test 1: Unsigned integer serialization.
    {
        Serializer serializer;
        serializer.Write(std::uint32_t{ 42 });

        Deserializer deserializer(serializer.GetBuffer());

        std::uint32_t value = 0;
        const ReadResult result = deserializer.Read(value);

        const bool testPassed = result && value == 42;

        std::cout << "[Test 1] UInt32 serialization: "
            << (testPassed ? "PASSED" : "FAILED")
            << '\n';

        allTestsPassed &= testPassed;
    }

    // Test 2: Signed integer serialization.
    {
        Serializer serializer;
        serializer.Write(std::int32_t{ -123 });

        Deserializer deserializer(serializer.GetBuffer());

        std::int32_t value = 0;
        const ReadResult result = deserializer.Read(value);

        const bool testPassed = result && value == -123;

        std::cout << "[Test 2] Int32 serialization: "
            << (testPassed ? "PASSED" : "FAILED")
            << '\n';

        allTestsPassed &= testPassed;
    }

    // Test 3: Float serialization.
    {
        Serializer serializer;
        serializer.Write(3.14f);

        Deserializer deserializer(serializer.GetBuffer());

        float value = 0.0f;
        const ReadResult result = deserializer.Read(value);

        const bool testPassed = result && value == 3.14f;

        std::cout << "[Test 3] Float serialization: "
            << (testPassed ? "PASSED" : "FAILED")
            << '\n';

        allTestsPassed &= testPassed;
    }

    // Test 4: Boolean serialization.
    {
        Serializer serializer;
        serializer.Write(true);

        Deserializer deserializer(serializer.GetBuffer());

        bool value = false;
        const ReadResult result = deserializer.Read(value);

        const bool testPassed = result && value;

        std::cout << "[Test 4] Bool serialization: "
            << (testPassed ? "PASSED" : "FAILED")
            << '\n';

        allTestsPassed &= testPassed;
    }

    // Test 5: Multiple values in sequence.
    {
        Serializer serializer;

        serializer.Write(std::uint8_t{ 10 });
        serializer.Write(std::uint32_t{ 250 });
        serializer.Write(false);

        Deserializer deserializer(serializer.GetBuffer());

        std::uint8_t byteValue = 0;
        std::uint32_t integerValue = 0;
        bool boolValue = true;

        const bool readSucceeded =
            deserializer.Read(byteValue) &&
            deserializer.Read(integerValue) &&
            deserializer.Read(boolValue);

        const bool testPassed =
            readSucceeded &&
            byteValue == 10 &&
            integerValue == 250 &&
            !boolValue &&
            deserializer.IsAtEnd();

        std::cout << "[Test 5] Multiple values: "
            << (testPassed ? "PASSED" : "FAILED")
            << '\n';

        allTestsPassed &= testPassed;
    }

    // Test 6: Reading beyond the end of the buffer.
    {
        Serializer serializer;
        serializer.Write(std::uint8_t{ 42 });

        Deserializer deserializer(serializer.GetBuffer());

        std::uint8_t firstValue = 0;
        std::uint32_t secondValue = 0;

        const bool firstReadSucceeded =
            deserializer.Read(firstValue);

        const ReadResult secondResult =
            deserializer.Read(secondValue);

        const bool testPassed =
            firstReadSucceeded &&
            !secondResult &&
            firstValue == 42 &&
            deserializer.GetPosition() == sizeof(std::uint8_t);

        std::cout << "[Test 6] End of buffer handling: "
            << (testPassed ? "PASSED" : "FAILED")
            << '\n';

        allTestsPassed &= testPassed;
    }

    return allTestsPassed;
}

// Temporary test entry point.
// Move this function to TestMain.cpp when adopting a test framework.
int main()
{
    std::cout << "=== Serialization Tests ===\n\n";

    const bool allTestsPassed = RunSerializationTests();

    std::cout << "\n============================\n";
    std::cout << (allTestsPassed
        ? "All tests passed."
        : "Some tests failed.") << '\n';

    return allTestsPassed ? 0 : 1;
}
