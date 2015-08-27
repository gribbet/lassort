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

	TileIndex(liblas::Point const &point, double tileSize): 
		i((int) (point.GetX()/tileSize)),
		j((int) (point.GetY()/tileSize)),
		k((int) (point.GetZ()/tileSize)) {
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
	Tile(boost::filesystem::path workDir):
		_count(0),
		path(workDir / boost::filesystem::unique_path("%%%%-%%%%-%%%%-%%%%.las")),
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
		_count++;
	}

	void flush() {
		if (points.size() == 0)
			return;
		if (writer == NULL) {
			liblas::Header header = liblas::Header(*points[0].GetHeader());
			header.SetCompressed(false);
			writer = new liblas::Writer(ofs, header);
		}
		for (long i = 0; i < points.size(); i++)
			writer->WritePoint(points[i]);
		points.clear();
		writeHeader();
	}

	long const count() {
		return _count;
	}
	
	uintmax_t const fileSize() {
		return boost::filesystem::file_size(path);
	}

	void write(liblas::Writer &writer) {
		std::ifstream ifs(path.string(), std::ios::in | std::ios::binary);

		liblas::ReaderFactory factory;
		liblas::Reader reader = factory.CreateWithStream(ifs);

		while(reader.ReadNextPoint())
			writer.WritePoint(reader.GetPoint());

		ifs.close();
	}
	
	void remove() {
		boost::filesystem::remove(path);
	}

private:
	long _count;
	std::vector<liblas::Point> points;
	boost::filesystem::path path;
	std::ofstream ofs;
	liblas::Writer* writer;

	void writeHeader() {
		liblas::Header header(writer->GetHeader());
		header.SetPointRecordsCount(count());
		writer->SetHeader(header);
		writer->WriteHeader();
	}
};

class Grid {
public:
	Grid(std::string workDir, double tileSize):
		tileSize(tileSize),
		workDir(boost::filesystem::path(workDir)),
		workDirCreated(boost::filesystem::create_directories(workDir)),
		count(0) {
	}

	~Grid() {
		flush();
		for(std::map<TileIndex, Tile*>::iterator it = tiles.begin();
			it != tiles.end(); ++it)
			delete it->second;
		tiles.clear();
		if (workDirCreated)
			boost::filesystem::remove(workDir);
	}

	void read(liblas::Reader &reader) {
	std::cout << "Tiled 0%" << std::flush;
		liblas::Header header = reader.GetHeader();
		long total = header.GetPointRecordsCount();
		long read = 0;
		while(reader.ReadNextPoint()) {
			add(reader.GetPoint());
			if (++read % 1000000 == 0) {
				flush();
				std::cout << "\rTiled " << (100 * read / total) << "%" << std::flush;
			}
		}
		flush();
		std::cout << "\rTiled 100%" << std::endl;
		count += read;
	}

	void write(liblas::Writer &writer) {
		std::cout << "Merged 0%" << std::flush;
		long written = 0;
		long lastChunk = -1;
		for(std::map<TileIndex, Tile*>::iterator it = tiles.begin();
			it != tiles.end(); ++it) {
			it->second->write(writer);
			written += it->second->count();
			it->second->remove();
			if (written / 1000000 != lastChunk) {
				lastChunk = written / 1000000;
				std::cout << "\rMerged " << (100 * written / count) << "%" << std::flush;
			}
		}

		std::cout << "\rMerged 100%" << std::endl;
	}
	
	long tileCount() {
		return tiles.size();
	}
	
	long averageTileCount() {
		long total = 0;
		for(std::map<TileIndex, Tile*>::iterator it = tiles.begin();
			it != tiles.end(); ++it)
			total += it->second->count();
		return total /= tileCount();
	}
	
	uintmax_t averageTileFileSize() {
		uintmax_t total = 0;
		for(std::map<TileIndex, Tile*>::iterator it = tiles.begin();
			it != tiles.end(); ++it)
			total += it->second->fileSize();
		return total /= tileCount();
	}

private: 
	const int tileSize;
	const boost::filesystem::path workDir;
	const bool workDirCreated;
	long count;
	std::map<TileIndex, Tile*> tiles;

	void add(liblas::Point const &point) {
		TileIndex index(point, tileSize);
		if (tiles.find(index) == tiles.end())
			tiles[index] = new Tile(workDir);
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
	Sorter(std::string input,
		   std::string output,
		   std::string workDir,
		   double tileSize = 0.0):
		input(input),
		output(output),
		workDir(workDir),
		tileSize(tileSize) {
	}

	void sort() {
		std::ifstream ifs(input, std::ios::in | std::ios::binary);

		liblas::ReaderFactory factory;
		liblas::Reader reader = factory.CreateWithStream(ifs);
		liblas::Header header = reader.GetHeader();
		
		header.SetCompressed(boost::filesystem::path(output).extension() == ".laz");
		
		double tileSize = this->tileSize;
		if (tileSize == 0.0)
			tileSize = estimateTileSize(reader);
		
		Grid grid(workDir, tileSize);
		grid.read(reader);

		ifs.close();
		
		std::cout << "Total tiles: " << grid.tileCount() << std::endl;
		std::cout << "Average tile count: " << grid.averageTileCount() << std::endl;
		std::cout << "Average tile size: " << (grid.averageTileFileSize()/1000000) << "MB" << std::endl;

		std::ofstream ofs(output, std::ios::out | std::ios::binary);
		liblas::Writer writer(ofs, header);
		grid.write(writer);
		
		ofs.close();
	}

private:
	const std::string input;
	const std::string output;
	const std::string workDir;
	const double tileSize;

	double estimateTileSize(liblas::Reader reader) {
		liblas::Header const& header = reader.GetHeader();

		liblas::Bounds<double> bounds = header.GetExtent();
		double approximatePointsPerTile = 2e6;
		double points = header.GetPointRecordsCount();
		double volume = (bounds.maxx() - bounds.minx())
			* (bounds.maxy() - bounds.miny())
			* (bounds.maxz() - bounds.minz());
		double tileSize = std::pow(volume / points * approximatePointsPerTile, 1.0/3.0);

		return tileSize;
	}
};

int main(int argc, char **argv) {
	double tileSize;
	std::string input;
	std::string output;
	std::string workDir;

	po::options_description desc("options");
	po::positional_options_description p;
	po::variables_map vm;
	desc.add_options() 
		("help,h", "Prints usage")
		("size,s", po::value<double>(&tileSize)->default_value(0.0), "Tile size")
		("input,i", po::value<std::string>(&input), "Input LAS file")
		("output,o", po::value<std::string>(&output)->default_value("sorted.las"), "Output LAS file")
		("work-dir,w", po::value<std::string>(&workDir)->default_value("temp"));
	p.add("input", 1);
	p.add("output", 1);
	
	try {
		po::store(po::command_line_parser(argc, argv)
			.options(desc)
			.positional(p)
			.run(), vm);
		po::notify(vm);

		if(vm.count("help") || !vm.count("input")) {
			std::cout<< "USAGE: lassort [options] input" << std::endl;
			std::cout << desc << std::endl;
			return 0;
		}
		
		Sorter(input, output, workDir, tileSize).sort();
		return 0;
				
	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}
}