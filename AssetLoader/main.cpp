/**
 * @file main.cpp
 * @brief Command line of the AssetLoader demo application.
 *
 *   AssetLoader.exe                       walk-through demo (archive in the temporary folder)
 *   AssetLoader.exe demo [archive]        same, with the archive written to `archive`
 *   AssetLoader.exe samples <directory>   writes sample_v1.aset and sample_v2.aset
 *   AssetLoader.exe inspect <archive>     prints the header and directory of an archive
 *   AssetLoader.exe migrate <in> <out>    converts an archive to the current format version
 *   AssetLoader.exe bench                 measures sizes, times and temporary memory
 */
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

#include "Commands.h"

namespace
{
	int PrintUsage()
	{
		std::cout <<
			"Usage:\n"
			"  AssetLoader.exe                       walk-through demo (archive in the temporary folder)\n"
			"  AssetLoader.exe demo [archive]        same, with the archive written to <archive>\n"
			"  AssetLoader.exe samples <directory>   writes sample_v1.aset and sample_v2.aset\n"
			"  AssetLoader.exe inspect <archive>     prints the header and directory of an archive\n"
			"  AssetLoader.exe migrate <in> <out>    converts an archive to the current format version\n"
			"  AssetLoader.exe bench                 measures sizes, times and temporary memory\n";
		return 1;
	}
}

int main(int argc, char* argv[])
{
	const std::vector<std::string_view> args(argv + 1, argv + argc);
	const std::filesystem::path defaultArchive = std::filesystem::temp_directory_path() / "AssetLoaderDemo.aset";

	if (args.empty())
	{
		return app::RunDemo(defaultArchive);
	}

	const std::string_view command = args[0];
	if (command == "demo" && args.size() <= 2)
	{
		return app::RunDemo(args.size() == 2 ? std::filesystem::path(args[1]) : defaultArchive);
	}
	if (command == "samples" && args.size() == 2)
	{
		return app::RunSamples(args[1]);
	}
	if (command == "inspect" && args.size() == 2)
	{
		return app::RunInspect(args[1]);
	}
	if (command == "migrate" && args.size() == 3)
	{
		return app::RunMigrate(args[1], args[2]);
	}
	if (command == "bench" && args.size() == 1)
	{
		return app::RunBenchmark();
	}
	return PrintUsage();
}
