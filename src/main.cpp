#include <iostream>

#include <boost/program_options.hpp>
#include <liblas/liblas.hpp>

namespace po = boost::program_options; 

int main(int argc, char **argv) {
	std::vector<std::string> source;
	float size;

	po::options_description desc("options"); 
	desc.add_options() 
		("help,h", "prints usage")
		("size,s", po::value<float>(&size), "Tile size") 
		("source", po::value<std::vector<std::string> >(), "Source LAS files.");
	po::positional_options_description p; 
	p.add("source", -1); 

	po::variables_map vm; 
	po::store(po::command_line_parser(argc, argv)
		.options(desc)
		.positional(p)
		.run(), vm); 
	po::notify(vm);

	if(vm.count("help") || !vm.count("source")) {
		std::cout << desc << std::endl;
		return 0;
	}

	if(vm.count("source"))
		source = vm["source"].as<std::vector<std::string> >();
	else {
		std::cerr << "source file parameter is missing" << std::endl;
		return 1;
	}

	for(int i = 0; i < source.size(); i++) {
		std::ifstream ifs;
		ifs.open(source[i], std::ios::in | std::ios::binary);

		liblas::ReaderFactory factory;
		liblas::Reader reader = factory.CreateWithStream(ifs);

		liblas::Header const& header = reader.GetHeader();

		std::cout << "File: " + source[i] << std::endl;
		std::cout << "Points count: " << header.GetPointRecordsCount() << std::endl;
		std::cout << std::endl;
	}
}