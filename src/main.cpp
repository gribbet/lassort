#include <iostream>
#include <cmath>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <liblas/liblas.hpp>
#include <boost/random/uniform_real.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/random/lagged_fibonacci.hpp>

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
		workDir(workDir) {
	}

	~Tile() {
		flush();
		remove();
	}

	void add(liblas::Point const &point) {
		points.push_back(point);
		_count++;
	}

	void flush() {
		if (points.size() == 0)
			return;
		
		auto header = liblas::Header(*points[0].GetHeader());
		header.SetCompressed(false);
		
		auto path = boost::filesystem::path(workDir / boost::filesystem::unique_path("%%%%%%%%.las"));
		std::ofstream ofs(path.string(), std::ios::out | std::ios::binary);
		
		auto writer = liblas::Writer(ofs, header);
		for(auto point: points)
			writer.WritePoint(point);
		
		points.clear();
		points.shrink_to_fit();
		
		paths.push_back(path);
	}

	long const count() {
		return _count;
	}
	
	uintmax_t const fileSize() {
		uintmax_t size = 0;
		for (auto path: paths)
			size += boost::filesystem::file_size(path);
		return size;
	}

	void write(liblas::Writer &writer) {
		for (auto path: paths)
			write(writer, path);
	}
	
	void remove() {
		for (auto path: paths)
			boost::filesystem::remove(path);
	}

private:
	long _count;
	std::vector<liblas::Point> points;
	std::vector<boost::filesystem::path> paths;
	boost::filesystem::path workDir;
	
	void write(liblas::Writer &writer, boost::filesystem::path path) {
		std::ifstream ifs(path.string(), std::ios::in | std::ios::binary);
		liblas::ReaderFactory factory;
		liblas::Reader reader = factory.CreateWithStream(ifs);

		while(reader.ReadNextPoint())
			writer.WritePoint(reader.GetPoint());

		ifs.close();
	}
};

class Grid {
public:
	Grid(std::string workDir, double tileSize, double thin):
		tileSize(tileSize),
		thin(thin),
		workDir(boost::filesystem::path(workDir)),
		workDirCreated(boost::filesystem::create_directories(workDir)),
		count(0) {
	}

	~Grid() {
		flush();
		for(auto pair: tiles)
			delete pair.second;
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
			if (thin == 0.0 || random() > thin) {
				add(reader.GetPoint());
				count++;
			}
			if (++read % 1000000 == 0) {
				flush();
				std::cout << "\rTiled " << (100 * read / total) << "%" << std::flush;
			}
		}
		flush();
		std::cout << "\rTiled 100%" << std::endl;
	}

	void write(liblas::Writer &writer) {
		std::cout << "Merged 0%" << std::flush;
		long written = 0;
		long lastChunk = -1;
		for(auto pair: tiles) {
			written += pair.second->count();
			pair.second->write(writer);
			pair.second->remove();
			if (written / 1000000 != lastChunk) {
				lastChunk = written / 1000000;
				std::cout << "\rMerged " << (100 * written / count) << "%" << std::flush;
			}
		}

		std::cout << "\rMerged 100%" << std::endl;
	}
	
	long total() {
		return count;
	}
	
	long tileCount() {
		return tiles.size();
	}
	
	long averageTileCount() {
		long total = 0;
		for(auto pair: tiles)
			total += pair.second->count();
		return total /= tileCount();
	}
	
	uintmax_t averageTileFileSize() {
		uintmax_t total = 0;
		for(auto pair: tiles)
			total += pair.second->fileSize();
		return total /= tileCount();
	}

private: 
	const double tileSize;
	const double thin;
	const boost::filesystem::path workDir;
	const bool workDirCreated;
	long count;
	std::map<TileIndex, Tile*> tiles;
	typedef boost::lagged_fibonacci607 base_generator_type;
	base_generator_type generator = base_generator_type();
	boost::uniform_01<base_generator_type> random = 
		boost::uniform_01<base_generator_type>(generator);

	void add(liblas::Point const &point) {
		TileIndex index(point, tileSize);
		if (tiles.find(index) == tiles.end())
			tiles[index] = new Tile(workDir);
		tiles[index]->add(point);
	}

	void flush() {
		for(auto pair: tiles)
			pair.second->flush();
	}
};

class Sorter {
public:
	Sorter(std::string input,
		   std::string output,
		   std::string workDir,
		   double tileSize = 0.0,
		   double thin = 0.0):
		input(input),
		output(output),
		workDir(workDir),
		tileSize(tileSize),
		thin(thin) {
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
		
		Grid grid(workDir, tileSize, thin);
		grid.read(reader);

		ifs.close();
		
		header.SetPointRecordsCount(grid.total());
		
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
	const double thin;

	double estimateTileSize(liblas::Reader reader) {
		liblas::Header const& header = reader.GetHeader();

		liblas::Bounds<double> bounds = header.GetExtent();
		double approximatePointsPerTile = 2e6;
		double points = header.GetPointRecordsCount() * (1.0 - thin);
		double volume = (bounds.maxx() - bounds.minx())
			* (bounds.maxy() - bounds.miny())
			* (bounds.maxz() - bounds.minz());
		double tileSize = std::pow(volume / points * approximatePointsPerTile, 1.0/3.0);

		return tileSize;
	}
};

int main(int argc, char **argv) {
	double tileSize;
	double thin;
	std::string input;
	std::string output;
	std::string workDir;

	po::options_description desc("options");
	po::positional_options_description p;
	po::variables_map vm;
	desc.add_options() 
		("help,h", "Prints usage")
		("size,s", po::value<double>(&tileSize)->default_value(0.0), "Tile size")
		("thin,t", po::value<double>(&thin)->default_value(0.0), "Thin percentage")
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
			std::cout<< "USAGE: lassort [options] input output" << std::endl;
			std::cout << desc << std::endl;
			return 0;
		}
		
		Sorter(input, output, workDir, tileSize, thin).sort();
		return 0;
				
	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}
}