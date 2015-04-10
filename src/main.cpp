#include <iostream>
#include <cmath>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <liblas/liblas.hpp>

#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

namespace po = boost::program_options;

class TileIndex {
public:
	TileIndex(int i, int j, int k): i(i), j(j), k(k) {
	}

	TileIndex(liblas::Point const &point, double spacing): 
		i((int) (point.GetX()/spacing)),
		j((int) (point.GetY()/spacing)),
		k((int) (point.GetZ()/spacing)) {
	}

	bool operator<(const TileIndex& b) const {
		return i < b.i
			|| (i == b.i && j < b.j)
			|| (i == b.i && j == b.j && k < b.k);
	}

private:
	const int i, j, k;
};

class Tile {
public:
	Tile(): 
		count(0), 
		path(boost::filesystem::unique_path()),
		writer(NULL) {
		ofs.open(path.string(), std::ios::out | std::ios::binary);
	}

	~Tile() {
		flush();
		if (writer != NULL)
			delete writer;
		boost::filesystem::remove(path);
	}

	void add(liblas::Point const &point) {
		points.push_back(point);
		count++;
	}

	void flush() {
		if (points.size() == 0)
			return;
		if (writer == NULL)
			writer = new liblas::Writer(ofs, liblas::Header(*points[0].GetHeader()));
		for (long i = 0; i < points.size(); i++)
			writer->WritePoint(points[i]);
		points.clear();
		writeHeader();
	}

	long const size() {
		return count;
	}

	void write(liblas::Writer &writer) {
		std::ifstream ifs(path.string(), std::ios::in | std::ios::binary);

		liblas::ReaderFactory factory;
		liblas::Reader reader = factory.CreateWithStream(ifs);

		while(reader.ReadNextPoint())
			writer.WritePoint(reader.GetPoint());

		ifs.close();
	}

private:
	long count;
	std::vector<liblas::Point> points;
	boost::filesystem::path path;
	std::ofstream ofs;
	liblas::Writer* writer;

	void writeHeader() {
		liblas::Header header(writer->GetHeader());
		header.SetPointRecordsCount(count);
		writer->SetHeader(header);
		writer->WriteHeader();
	}
};

class Grid {
public:
	Grid(double spacing): spacing(spacing), count(0) {
	}

	~Grid() {
		flush();
		for(std::map<TileIndex, Tile*>::iterator it = tiles.begin();
			it != tiles.end(); ++it)
			delete it->second;
		tiles.clear();
	}

	void read(liblas::Reader &reader) {
		liblas::Header header = reader.GetHeader();
		long total = header.GetPointRecordsCount();
		long read = 0;
		while(reader.ReadNextPoint()) {
			add(reader.GetPoint());
			if (++read % 1000000 == 0) {
				flush();
				std::cout << "Tiled " << (100 * read / total) << "%" << std::endl;
			}
		}
		flush();
		std::cout << "Tiling complete" << std::endl;
		count += read;
	}

	void write(liblas::Writer &writer) {
		long written = 0;
		for(std::map<TileIndex, Tile*>::iterator it = tiles.begin();
			it != tiles.end(); ++it) {
			it->second->write(writer);
			written += it->second->size();
			std::cout << "Sorted " << (100 * written / count) << "%" << std::endl;
		}
	}

private: 
	const int spacing;
	long count;
	std::map<TileIndex, Tile*> tiles;

	void add(liblas::Point const &point) {
		TileIndex index(point, spacing);
		if (tiles.find(index) == tiles.end())
			tiles[index] = new Tile();
		tiles[index]->add(point);
	}

	void flush() {
		for(std::map<TileIndex, Tile*>::iterator it = tiles.begin();
			it != tiles.end(); ++it)
			it->second->flush();
	}
};

class Sorter {
public:
	Sorter(boost::filesystem::path source, boost::filesystem::path destination): 
		source(source), destination(destination) {
	}

	void sort() {
		std::ifstream ifs(source.string(), std::ios::in | std::ios::binary);

		liblas::ReaderFactory factory;
		liblas::Reader reader = factory.CreateWithStream(ifs);
		liblas::Header header = reader.GetHeader();
		
		Grid grid(estimateSpacing(reader));
		grid.read(reader);

		ifs.close();

		std::ofstream ofs(destination.string(), std::ios::out | std::ios::binary);
		liblas::Writer writer(ofs, header);
		grid.write(writer);
		
		ofs.close();
	}

private: 
	boost::filesystem::path source;
	boost::filesystem::path destination;

	double estimateSpacing(liblas::Reader reader) {
		liblas::Header const& header = reader.GetHeader();

		liblas::Bounds<double> bounds = header.GetExtent();
		double approximatePointsPerTile = 1e7;
		double points = header.GetPointRecordsCount();
		double volume = (bounds.maxx() - bounds.minx())
			* (bounds.maxy() - bounds.miny())
			* (bounds.maxz() - bounds.minz());
		double spacing = std::pow(volume / points * approximatePointsPerTile, 1.0/3.0);

		std::cout << "Spacing: " << spacing << std::endl;

		return spacing;
	}
};

int main(int argc, char **argv) {
	std::vector<std::string> sources;
	float size;

	po::options_description desc("options"); 
	desc.add_options() 
		("help,h", "prints usage")
		("sources", po::value<std::vector<std::string> >(), "Source LAS/LAZ files.");
	po::positional_options_description p; 
	p.add("sources", -1); 

	po::variables_map vm; 
	po::store(po::command_line_parser(argc, argv)
		.options(desc)
		.positional(p)
		.run(), vm); 
	po::notify(vm);

	if(vm.count("help") || !vm.count("sources")) {
		std::cout << desc << std::endl;
		return 0;
	}

	if(vm.count("sources"))
		sources = vm["sources"].as<std::vector<std::string> >();
	else {
		std::cerr << "source file parameter is missing" << std::endl;
		return 1;
	}

	try {
		for(int i = 0; i < sources.size(); i++) {
			boost::filesystem::path source = boost::filesystem::path(sources[i]);
			boost::filesystem::path destination = boost::filesystem::path(source.stem().string() + "-sorted" + source.extension().string());
			Sorter(source, destination).sort();
		}
	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}
}