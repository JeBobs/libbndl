#include <libbndl/bundle.hpp>
#include <iostream>
#include <iomanip>
#include <cxxopts.hpp>

using namespace libbndl;

int main(int argc, char** argv)
{
	//option parsing
	cxxopts::Options options("bndl_util", "A program to work with Burnout Paradise BUNDLE archives.");
	options.add_options()
		("e,extract", "Extract the archive")
		("p,pack", "Pack a folder structure to a BUNDLE archive")
		("f,file", "Name of the archive that should be extracted/generated", cxxopts::value<std::string>())
		("s,search","Search for an entry", cxxopts::value<std::string>())
		("l,list","List all entries");

	options.parse(argc, argv);
	if(options.count("file")==0)
	{
		std::cout << "Please specify an input file." << std::endl << options.help() << std::endl;
		return EXIT_FAILURE;
	}

	bool extract = options["pack"].as<bool>();
	bool pack = options["pack"].as<bool>();
	bool list = options["list"].as<bool>();
	std::string file = options["file"].as<std::string>();
	std::string search = options["search"].as<std::string>();
	bool bsearch = search.size()>0;
	
	if(!(pack^extract^list^bsearch))
	{
		std::cout << "Please specify exactly one operation that should be executed" << std::endl
		<< options.help() << std::endl;
	}

	Bundle arch;
	if(!pack)
	{
		if(!arch.Load(file))
		{
			std::cout << "Failed to open "<< file << std::endl;
			return -1;
		}
		if(list)
		{
			std::cout.fill('-');
			std::cout << "NAME" << std::setw(70) << "FILE TYPE" << std::endl;
			std::cout.fill(' ');
			for (const auto &fileID : arch.ListFileIDs())
			{
				Bundle::EntryInfo info = arch.GetInfo(fileID);
				std::cout<< std::left << std::setw(70) << std::hex << fileID << std::right << " " << info.fileType << std::dec << std::endl;
			}
		}
	}
	

	return 0;
}