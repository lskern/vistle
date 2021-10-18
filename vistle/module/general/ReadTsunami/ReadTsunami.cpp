/**************************************************************************\
 **                                                                        **
 **                                                                        **
 ** Description: Read module for ChEESE tsunami nc-files                   **
 **                                                                        **
 **                                                                        **
 **                                                                        **
 **                                                                        **
 **                                                                        **
 ** Author:    Marko Djuric                                                **
 **                                                                        **
 **                                                                        **
 **                                                                        **
 ** Date:  25.01.2021                                                      **
\**************************************************************************/

//header
#include "ReadTsunami.h"

//vistle
#include "vistle/core/database.h"
#include "vistle/core/index.h"
#include "vistle/core/object.h"
#include "vistle/core/parameter.h"
#include "vistle/core/polygons.h"
#include "vistle/core/scalar.h"
#include "vistle/core/vec.h"
#include "vistle/module/module.h"

//vistle-module-util
#include "vistle/module/general/utils/ghost.h"
/* #include "vistle/module/general/utils/tsafe_ptr.h" */

//vistle-module-netcdf-util
/* #include "vistle/module/general/utils/vistle_netcdf.h" */

//std
#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <string>
#include <cstddef>
#include <vector>

using namespace vistle;
using namespace netCDF;

MODULE_MAIN(ReadTsunami)
namespace {

constexpr auto ETA{"eta"};

/* typedef vistle::netcdf::VNcVar VNcVar; */
/* typedef vistle::netcdf::VNcVar::NcVec_size NcVec_size; */
/* typedef vistle::netcdf::VNcVar::NcVec_diff NcVec_diff; */
typedef safe_ptr<NcVar> VNcVar;
typedef std::vector<size_t> NcVec_size;
typedef std::vector<ptrdiff_t> NcVec_diff;

template<class T>
struct NTimesteps {
private:
    const T first;
    const T last;
    const T inc;

public:
    NTimesteps(T f, T l, T i): first(f), last(l), inc(i) {}
    void operator()(T &nTimestep) const
    {
        nTimestep = (inc > 0 && last >= first) || (inc < 0 && last <= first) ? ((last - first) / inc + 1) : -1;
    }
};

} // namespace

ReadTsunami::ReadTsunami(const std::string &name, int moduleID, mpi::communicator comm)
: vistle::Reader(name, moduleID, comm), seaTimeConn(false)
{
    // file-browser
    m_filedir = addStringParameter("file_dir", "NC File directory", "/data/ChEESE/tsunami/NewData/cadiz_5m.nc",
                                   Parameter::Filename);

    //ghost
    /* m_ghostTsu = addIntParameter("ghost_old", "Show ghostcells.", 1, Parameter::Boolean); */
    m_fill = addIntParameter("fill", "Replace filterValue.", 1, Parameter::Boolean);
    m_verticalScale = addFloatParameter("VerticalScale", "Vertical Scale parameter sea", 1.0);

    // define ports
    m_seaSurface_out = createOutputPort("Sea surface", "2D Grid Sea (Polygons)");
    m_groundSurface_out = createOutputPort("Ground surface", "2D Sea floor (Polygons)");

    // block size
    m_blocks[0] = addIntParameter("blocks latitude", "number of blocks in lat-direction", 2);
    m_blocks[1] = addIntParameter("blocks longitude", "number of blocks in lon-direction", 2);
    setParameterRange(m_blocks[0], Integer(1), Integer(999999));
    setParameterRange(m_blocks[1], Integer(1), Integer(999999));

    //fillvalue
    addFloatParameter("fillValue", "ncFile fillValue offset for eta", -9999.f);
    addFloatParameter("fillValueNew", "set new fillValue offset for eta", 0.0f);

    //bathymetryname
    m_bathy = addStringParameter("bathymetry ", "Select bathymetry stored in netCDF", "", Parameter::Choice);

    //scalar
    initScalarParamReader();

    // timestep built-in params
    setParameterRange(m_first, Integer(0), Integer(9999999));
    setParameterRange(m_last, Integer(-1), Integer(9999999));
    setParameterRange(m_increment, Integer(1), Integer(9999999));

    // observer these parameters
    observeParameter(m_filedir);
    observeParameter(m_blocks[0]);
    observeParameter(m_blocks[1]);
    observeParameter(m_verticalScale);

    /* setParallelizationMode(ParallelizeBlocks); */
    setParallelizationMode(ParallelizeDIYBlocks);
}

/**
 * @brief Destructor.
 */
ReadTsunami::~ReadTsunami()
{}

/**
 * @brief Initialize scalar choice parameter and ports.
 */
void ReadTsunami::initScalarParamReader()
{
    constexpr auto scalar_name{"Scalar "};
    constexpr auto scalarPort_name{"Scalar port "};
    constexpr auto scalarPort_descr{"Port for scalar number "};

    for (Index i = 0; i < NUM_SCALARS; ++i) {
        const std::string i_str{std::to_string(i)};
        const std::string &scName = scalar_name + i_str;
        const std::string &portName = scalarPort_name + i_str;
        const std::string &portDescr = scalarPort_descr + i_str;
        m_scalars[i] = addStringParameter(scName, "Select scalar.", "", Parameter::Choice);
        m_scalarsOut[i] = createOutputPort(portName, portDescr);
        observeParameter(m_scalars[i]);
    }
}

/**
 * @brief Open Nc File and set pointer ncDataFile.
 *
 * @return true if its not empty or cannot be opened.
 */
bool ReadTsunami::openNcFile(std::shared_ptr<NcFile> file)
{
    std::string sFileName = m_filedir->getValue();

    if (sFileName.empty()) {
        printRank0("NetCDF filename is empty!");
        return false;
    } else {
        try {
            file->open(sFileName.c_str(), NcFile::read);
            printRank0("Reading File: " + sFileName);
        } catch (...) {
            printRank0("Couldn't open NetCDF file!");
            return false;
        }
        if (file->getVarCount() == 0) {
            printRank0("empty NetCDF file!");
            return false;
        } else
            return true;
    }
}

bool ReadTsunami::openNcFile(safe_ptr<NcFile> file)
{
    std::string sFileName = m_filedir->getValue();

    if (sFileName.empty()) {
        printRank0("NetCDF filename is empty!");
        return false;
    } else {
        try {
            file->open(sFileName.c_str(), NcFile::read);
            printRank0("Reading File: " + sFileName);
        } catch (...) {
            printRank0("Couldn't open NetCDF file!");
            return false;
        }
        if (file->getVarCount() == 0) {
            printRank0("empty NetCDF file!");
            return false;
        } else
            return true;
    }
}


/**
 * @brief Print string only on rank 0.
 *
 * @param str Format str to print.
 */
template<class... Args>
void ReadTsunami::printRank0(const std::string &str, Args... args) const
{
    if (rank() == 0)
        sendInfo(str, args...);
}

/**
  * @brief Prints current rank and the number of all ranks to the console.
  */
void ReadTsunami::printMPIStats() const
{
    printRank0("Current Rank: " + std::to_string(rank()) + " Processes (MPISIZE): " + std::to_string(size()));
}

/**
  * @brief Prints thread-id to /var/tmp/<user>/ReadTsunami-*.
  */
inline void ReadTsunami::printThreadStats() const
{
    std::cout << std::this_thread::get_id() << '\n';
}

/**
  * @brief Called when any of the reader parameter changing.
  *
  * @param: Parameter that got changed.
  * @return: true if all essential parameters could be initialized.
  */
bool ReadTsunami::examine(const vistle::Parameter *param)
{
    if (!param || param == m_filedir) {
        printMPIStats();

        if (!inspectNetCDFVars())
            return false;
    }

    const int &nBlocks = m_blocks[0]->getValue() * m_blocks[1]->getValue();
    // TODO: NetCDF not threadsafe => implement lock mechanisim?
    setDimDomain(2);
    setHandlePartitions(nBlocks > size());
    setPartitions(nBlocks);
    return true;
    /* if (nBlocks <= size()) { */
    /*     setDimDomain(2); */
    /*     setPartitions(nBlocks); */
    /*     return true; */
    /* } else { */
    /*     printRank0("Total number of blocks should equal MPISIZE or less."); */
    /*     return false; */
    /* } */
}

/**
 * @brief Inspect netCDF variables stored in file.
 */
bool ReadTsunami::inspectNetCDFVars()
{
    std::shared_ptr<NcFile> ncFile(new NcFile());
    if (!openNcFile(ncFile))
        return false;

    const int &maxTime = ncFile->getDim("time").getSize();
    setTimesteps(maxTime);

    //set block diy parameter
    const int &maxLat = ncFile->getDim("lat").getSize();
    const int &maxLon = ncFile->getDim("lon").getSize();
    std::vector minDomain{0, 0};
    std::vector maxDomain{maxLat, maxLon};
    setMaxDomain(maxDomain);
    setMinDomain(minDomain);

    //scalar inspection
    std::vector<std::string> scalarChoiceVec;
    std::vector<std::string> bathyChoiceVec;
    auto strContains = [](const std::string &name, const std::string &contains) {
        return name.find(contains) != std::string::npos;
    };
    auto latLonContainsGrid = [=](auto &name, int i) {
        if (strContains(name, "grid"))
            m_latLon_Ground[i] = name;
        else
            m_latLon_Sea[i] = name;
    };

    //delete previous choicelists.
    m_bathy->setChoices(std::vector<std::string>());
    for (const auto &scalar: m_scalars)
        scalar->setChoices(std::vector<std::string>());

    //read names of scalars
    for (auto &name_val: ncFile->getVars()) {
        auto &name = name_val.first;
        auto &val = name_val.second;
        if (strContains(name, "lat"))
            latLonContainsGrid(name, 0);
        else if (strContains(name, "lon"))
            latLonContainsGrid(name, 1);
        else if (strContains(name, "bathy"))
            bathyChoiceVec.push_back(name);
        else if (val.getDimCount() == 2) // for now: only scalars with 2 dim depend on lat lon.
            scalarChoiceVec.push_back(name);
    }

    //init choice param with scalardata
    setParameterChoices(m_bathy, bathyChoiceVec);

    for (auto &scalar: m_scalars)
        setParameterChoices(scalar, scalarChoiceVec);

    ncFile->close();

    return true;
}

/**
  * @brief Set 2D coordinates for given polygon.
  *
  * @poly: Pointer on Polygon.
  * @dim: Dimension of coordinates.
  * @coords: Vector which contains coordinates.
  * @zCalc: Function for computing z-coordinate.
  */
template<class T, class U>
void ReadTsunami::contructLatLonSurface(Polygons::ptr poly, const Dim<T> &dim, const std::vector<U> &coords,
                                        const zCalcFunc &zCalc)
{
    int n = 0;
    auto sx_coord = poly->x().data(), sy_coord = poly->y().data(), sz_coord = poly->z().data();
    for (size_t i = 0; i < dim.dimLat; i++)
        for (size_t j = 0; j < dim.dimLon; j++, n++) {
            sx_coord[n] = coords.at(1)[j];
            sy_coord[n] = coords.at(0)[i];
            sz_coord[n] = zCalc(i, j);
        }
}

/**
  * @brief Set the connectivitylist for given polygon for 2 dimensions and 4 Corners.
  *
  * @poly: Pointer on Polygon.
  * @dim: Dimension of vertice list.
  */
template<class T>
void ReadTsunami::fillConnectListPoly2Dim(Polygons::ptr poly, const Dim<T> &dim)
{
    int n = 0;
    auto verticeConnectivityList = poly->cl().data();
    for (size_t j = 1; j < dim.dimLat; j++)
        for (size_t k = 1; k < dim.dimLon; k++) {
            verticeConnectivityList[n++] = (j - 1) * dim.dimLon + (k - 1);
            verticeConnectivityList[n++] = j * dim.dimLon + (k - 1);
            verticeConnectivityList[n++] = j * dim.dimLon + k;
            verticeConnectivityList[n++] = (j - 1) * dim.dimLon + k;
        }
}

/**
  * @brief Set number of vertices which represent a polygon.
  *
  * @poly: Pointer on Polygon.
  * @numCorner: number of corners.
  */
template<class T>
void ReadTsunami::fillPolyList(Polygons::ptr poly, const T &numCorner)
{
    std::generate(poly->el().begin(), poly->el().end(), [n = 0, &numCorner]() mutable { return n++ * numCorner; });
}

/**
 * @brief Generate surface from polygons.
 *
 * @polyData: Data for creating polygon-surface (number elements, number corners, number vertices).
 * @dim: Dimension in lat and lon.
 * @coords: coordinates for polygons (lat, lon).
 * @zCalc: Function for computing z-coordinate.
 * @return vistle::Polygons::ptr
 */
template<class U, class T, class V>
Polygons::ptr ReadTsunami::generateSurface(const PolygonData<U> &polyData, const Dim<T> &dim,
                                           const std::vector<V> &coords, const zCalcFunc &zCalc)
{
    Polygons::ptr surface(new Polygons(polyData.numElements, polyData.numCorners, polyData.numVertices));

    // fill coords 2D
    contructLatLonSurface(surface, dim, coords, zCalc);

    // fill vertices
    fillConnectListPoly2Dim(surface, dim);

    // fill the polygon list
    fillPolyList(surface, 4);

    return surface;
}

/**
  * @brief Generates NcVarParams struct which contains start, count and stride values computed based on given parameters.
  *
  * @dim: Current dimension of NcVar.
  * @ghost: Number of ghost cells to add.
  * @numDimBlocks: Total number of blocks for this direction.
  * @partition: Partition scalar.
  * @return: NcVarParams object.
  */
template<class T, class PartionIdx>
auto ReadTsunami::generateNcVarExt(const netCDF::NcVar &ncVar, const T &dim, const T &ghost, const T &numDimBlock,
                                   const PartionIdx &partition) const
{
    T count = dim / numDimBlock;
    T start = partition * count;
    structured_ghost_addition(start, count, dim, ghost);
    return NcVarExtended(ncVar, start, count);
}

/**
 * @brief Called once before read. Checks if timestep polygon computation can be skipped.
 *
 * @return true if everything is prepared.
 */
bool ReadTsunami::prepareRead()
{
    if (!openNcFile(ncFile))
        return false;
    seaTimeConn = m_seaSurface_out->isConnected();
    for (auto scalar_out: m_scalarsOut)
        seaTimeConn = seaTimeConn || scalar_out->isConnected();
    return true;
}

/**
  * @brief Called for each timestep and for each block (MPISIZE).
  *
  * @token: Ref to internal vistle token.
  * @timestep: current timestep.
  * @block: current block number of parallelization.
  * @return: true if all data is set and valid.
  */
bool ReadTsunami::read(Token &token, int timestep, int block)
{
    return computeBlock(token, block, timestep);
}

bool ReadTsunami::readDIY(const Bounds &bounds, Token &token, int timestep, int block)
{
    return computeBlockDIY(bounds, token, timestep, block);
}

bool ReadTsunami::computeBlockDIY(const Bounds &bounds, Token &token, int timestep, int block)
{
    if (timestep == -1)
        return computeInitialDIY(bounds, token, block);
    else if (seaTimeConn) {
        printRank0("(DIY) reading timestep: " + std::to_string(timestep));
        return computeTimestepDIY(token, timestep, block);
    }
    return true;
}

bool ReadTsunami::computeInitialDIY(const Bounds &bounds, Token &token, int block)
{
    /* std::shared_ptr<NcFile> ncFile(new NcFile()); */
    /* safe_ptr<NcFile> ncFile; */
    /* if (!openNcFile(ncFile)) */
    /*     return false; */

    // get nc var objects ref
    const auto &latvar = ncFile->getVar(m_latLon_Sea[0]);
    const auto &lonvar = ncFile->getVar(m_latLon_Sea[1]);
    const auto &grid_lat = ncFile->getVar(m_latLon_Ground[0]);
    const auto &grid_lon = ncFile->getVar(m_latLon_Ground[1]);
    const auto &bathymetryvar = ncFile->getVar(m_bathy->getValue());
    const auto &eta = ncFile->getVar(ETA);

    // compute current time parameters
    const ptrdiff_t &incrementTimestep = m_increment->getValue();
    const size_t &firstTimestep = m_first->getValue();
    size_t lastTimestep = m_last->getValue();
    size_t nTimesteps{0};
    computeActualLastTimestep(incrementTimestep, firstTimestep, lastTimestep, nTimesteps);

    // dimension from lat and lon variables
    const Dim<size_t> dimSea(latvar.getDim(0).getSize(), lonvar.getDim(0).getSize());
    /* const Dim<size_t> dimSea(latvar->getDim(0).getSize(), lonvar->getDim(0).getSize()); */

    // get dim from grid_lon & grid_lat
    const Dim<size_t> dimGround(grid_lat.getDim(0).getSize(), grid_lon.getDim(0).getSize());
    /* const Dim<size_t> dimGround(grid_lat->getDim(0).getSize(), grid_lon->getDim(0).getSize()); */

    const size_t &latMin = bounds.min[0];
    const size_t &latMax = bounds.max[0];
    const size_t &lonMin = bounds.min[1];
    const size_t &lonMax = bounds.max[1];
    //testing only  => ground dim = sea dim

    auto countLat{latMax - latMin};
    auto countLon{lonMax - lonMin};
    if (latMin + countLat == latMax)
        --countLat;
    if (lonMin + countLon == lonMax)
        --countLon;

    // num of polygons for sea & grnd
    const size_t &numPolyGround = (countLat - 1) * (countLon - 1);
    const size_t &numPolySea = numPolyGround;

    // vertices sea & grnd
    const size_t &verticesGround = countLat * countLon;
    verticesSea = countLat * countLon;

    // storage for read in values from ncdata
    std::vector<float> vecLat(countLat), vecLon(countLon), vecLatGrid(countLat), vecLonGrid(countLon),
        vecDepth(verticesGround);

    //************* read ncdata into float-pointer *************//
    {
        // read bathymetry
        {
            const NcVec_size vecStartBathy{latMin, lonMin};
            const NcVec_size vecCountBathy{countLat, countLon};
            const NcVec_diff vecStrideBathy{1, 1};
            bathymetryvar.getVar(vecStartBathy, vecCountBathy, vecStrideBathy, vecDepth.data());
            /* bathymetryvar->getVar(vecStartBathy, vecCountBathy, vecStrideBathy, vecDepth.data()); */
            sendInfo("no crash bathy");
        }

        // read eta
        {
            vecEta.resize(nTimesteps * verticesSea);
            const NcVec_size vecStartEta{firstTimestep, latMin, lonMin};
            const NcVec_size vecCountEta{nTimesteps, countLat, countLon};
            const NcVec_diff vecStrideEta{incrementTimestep, 1, 1};
            if (seaTimeConn) {
                eta.getVar(vecStartEta, vecCountEta, vecStrideEta, vecEta.data());
                /* eta->getVar(vecStartEta, vecCountEta, vecStrideEta, vecEta.data()); */

                //filter fillvalue
                if (m_fill->getValue()) {
                    const float &fillValNew = getFloatParameter("fillValueNew");
                    const float &fillVal = getFloatParameter("fillValue");
                    //TODO: Bad! needs rework.
                    std::replace(vecEta.begin(), vecEta.end(), fillVal, fillValNew);
                }
            }
            sendInfo("no crash eta");
        }

        // read lat, lon, grid_lat, grid_lon
        NcVec_diff stride{1};
        latvar.getVar(NcVec_size{latMin}, NcVec_size{countLat}, stride, vecLat.data());
        lonvar.getVar(NcVec_size{lonMin}, NcVec_size{countLon}, stride, vecLon.data());
        grid_lat.getVar(NcVec_size{latMin}, NcVec_size{countLat}, stride, vecLatGrid.data());
        grid_lon.getVar(NcVec_size{lonMin}, NcVec_size{countLon}, stride, vecLonGrid.data());
        /* latvar->getVar(NcVec_size{latMin}, NcVec_size{countLat}, stride, vecLat.data()); */
        /* lonvar->getVar(NcVec_size{lonMin}, NcVec_size{countLon}, stride, vecLon.data()); */
        /* grid_lat->getVar(NcVec_size{latMin}, NcVec_size{countLat}, stride, vecLatGrid.data()); */
        /* grid_lon->getVar(NcVec_size{lonMin}, NcVec_size{countLon}, stride, vecLonGrid.data()); */
        sendInfo("no crash other");
    }

    //************* create Polygons ************//
    {
        std::vector coords{vecLat.data(), vecLon.data()};

        //************* create sea *************//
        {
            const auto &seaDim = Dim(countLat, countLon);
            const auto &polyDataSea = PolygonData(numPolySea, numPolySea * 4, verticesSea);
            auto seaZCalcDiy = [](size_t x, size_t y) {
                return 0;
            };
            ptr_sea = generateSurface(polyDataSea, seaDim, coords, seaZCalcDiy);
        }

        //************* create grnd *************//
        coords[0] = vecLatGrid.data();
        coords[1] = vecLonGrid.data();

        const auto &scale = m_verticalScale->getValue();
        const auto &grndDim = Dim(countLat, countLon);
        const auto &polyDataGround = PolygonData(numPolyGround, numPolyGround * 4, verticesGround);
        auto grndZCalcDiy = [&vecDepth, &countLon, &scale](size_t j, size_t k) {
            return -vecDepth[j * countLon + k] * scale;
        };
        auto ptr_grnd = generateSurface(polyDataGround, grndDim, coords, grndZCalcDiy);

        //************* create selected scalars *************//
        const NcVec_size vecScalarStart{latMin, lonMin};
        const NcVec_size vecScalarCount{countLat, countLon};
        std::vector<float> vecScalar(verticesGround);
        for (size_t i = 0; i < NUM_SCALARS; ++i) {
            if (!m_scalarsOut[i]->isConnected())
                continue;
            const auto &scName = m_scalars[i]->getValue();
            /* const NcVar &val = ncFile->getVar(scName); */
            const auto &val = ncFile->getVar(scName);
            Vec<Scalar>::ptr ptr_scalar(new Vec<Scalar>(verticesSea));
            ptr_Scalar[i] = ptr_scalar;
            auto scX = ptr_scalar->x().data();
            val.getVar(vecScalarStart, vecScalarCount, NcVec_diff{1}, scX);
            /* val->getVar(vecScalarStart, vecScalarCount, NcVec_diff{1}, scX); */
            printRank0("no crash val");

            //set some meta data
            ptr_scalar->addAttribute("_species", scName);
            ptr_scalar->setTimestep(-1);
            ptr_scalar->setBlock(block);
        }

        // add ground data to port
        if (m_groundSurface_out->isConnected()) {
            ptr_grnd->setBlock(block);
            ptr_grnd->setTimestep(-1);
            ptr_grnd->updateInternals();
            token.addObject(m_groundSurface_out, ptr_grnd);
        }
    }

    ncFile->close();
    return true;
}

bool ReadTsunami::computeTimestepDIY(Token &token, int timestep, int block)
{
    return computeTimestep<int, size_t>(token, block, timestep);
}

/**
  * @brief Computing per block.
  *
  * @token: Ref to internal vistle token.
  * @blockNum: current block number of parallelization.
  * @timestep: current timestep.
  * @return: true if all data is set and valid.
  */
template<class T, class U>
bool ReadTsunami::computeBlock(Reader::Token &token, const T &blockNum, const U &timestep)
{
    if (timestep == -1)
        return computeInitial(token, blockNum);
    else if (seaTimeConn) {
        printRank0("reading timestep: " + std::to_string(timestep));
        return computeTimestep<int, size_t>(token, blockNum, timestep);
    }
    return true;
}

/**
 * @brief Compute actual last timestep.
 * @param incrementTimestep Stepwidth.
 * @param firstTimestep first timestep.
 * @param lastTimestep last timestep selected.
 * @param nTimesteps Storage value for number of timesteps.
 */
void ReadTsunami::computeActualLastTimestep(const ptrdiff_t &incrementTimestep, const size_t &firstTimestep,
                                            size_t &lastTimestep, size_t &nTimesteps)
{
    if (lastTimestep < 0)
        lastTimestep--;

    m_actualLastTimestep = lastTimestep - (lastTimestep % incrementTimestep);
    NTimesteps<size_t>(firstTimestep, m_actualLastTimestep, incrementTimestep)(nTimesteps);
}

/**
 * @brief Compute the unique block partition index for current block.
 *
 * @tparam Iter Iterator.
 * @param blockNum Current block.
 * @param ghost Initial number of ghostcells.
 * @param nLatBlocks Storage for number of blocks for latitude.
 * @param nLonBlocks Storage for number of blocks for longitude.
 * @param blockPartitionIterFirst Start iterator for storage partition indices.
 */
template<class Iter>
void ReadTsunami::computeBlockPartition(const int blockNum, vistle::Index &nLatBlocks, vistle::Index &nLonBlocks,
                                        Iter blockPartitionIterFirst)
{
    std::array<Index, NUM_BLOCKS> blocks;
    for (int i = 0; i < NUM_BLOCKS; i++)
        blocks[i] = m_blocks[i]->getValue();

    nLatBlocks = blocks[0];
    nLonBlocks = blocks[1];

    structured_block_partition(blocks.begin(), blocks.end(), blockPartitionIterFirst, blockNum);
}

/**
  * @brief Generates the initial polygon surfaces for sea and ground and adds only ground to scene.
  *
  * @token: Ref to internal vistle token.
  * @blockNum: current block number of parallel process.
  * @return: true if all date could be initialized.
  */
template<class T>
bool ReadTsunami::computeInitial(Token &token, const T &blockNum)
{
    /* std::shared_ptr<NcFile> ncFile(new NcFile()); */
    /* if (!openNcFile(ncFile)) */
    /*     return false; */

    // get nc var objects ref
    const NcVar &latvar = ncFile->getVar(m_latLon_Sea[0]);
    const NcVar &lonvar = ncFile->getVar(m_latLon_Sea[1]);
    const NcVar &grid_lat = ncFile->getVar(m_latLon_Ground[0]);
    const NcVar &grid_lon = ncFile->getVar(m_latLon_Ground[1]);
    const NcVar &bathymetryvar = ncFile->getVar(m_bathy->getValue());
    const NcVar &eta = ncFile->getVar(ETA);

    // compute current time parameters
    const ptrdiff_t &incrementTimestep = m_increment->getValue();
    const size_t &firstTimestep = m_first->getValue();
    size_t lastTimestep = m_last->getValue();
    size_t nTimesteps{0};
    computeActualLastTimestep(incrementTimestep, firstTimestep, lastTimestep, nTimesteps);

    // compute partition borders => structured grid
    Index nLatBlocks{0};
    Index nLonBlocks{0};
    std::array<Index, NUM_BLOCKS> bPartitionIdx;
    computeBlockPartition(blockNum, nLatBlocks, nLonBlocks, bPartitionIdx.begin());

    // dimension from lat and lon variables
    const Dim<size_t> dimSea(latvar.getDim(0).getSize(), lonvar.getDim(0).getSize());

    // get dim from grid_lon & grid_lat
    const Dim<size_t> dimGround(grid_lat.getDim(0).getSize(), grid_lon.getDim(0).getSize());

    size_t ghost{0};
    if (m_ghostTsu->getValue() == 1 && !(nLatBlocks == 1 && nLonBlocks == 1))
        ghost++;

    // count and start vals for lat and lon for sea polygon
    const auto latSea = generateNcVarExt<size_t, Index>(latvar, dimSea.dimLat, ghost, nLatBlocks, bPartitionIdx[0]);
    const auto lonSea = generateNcVarExt<size_t, Index>(lonvar, dimSea.dimLon, ghost, nLonBlocks, bPartitionIdx[1]);

    // count and start vals for lat and lon for ground polygon
    const auto latGround =
        generateNcVarExt<size_t, Index>(grid_lat, dimGround.dimLat, ghost, nLatBlocks, bPartitionIdx[0]);
    const auto lonGround =
        generateNcVarExt<size_t, Index>(grid_lon, dimGround.dimLon, ghost, nLonBlocks, bPartitionIdx[1]);

    // num of polygons for sea & grnd
    const size_t &numPolySea = (latSea.count - 1) * (lonSea.count - 1);
    const size_t &numPolyGround = (latGround.count - 1) * (lonGround.count - 1);

    // vertices sea & grnd
    verticesSea = latSea.count * lonSea.count;
    const size_t &verticesGround = latGround.count * lonGround.count;

    // storage for read in values from ncdata
    std::vector<float> vecLat(latSea.count), vecLon(lonSea.count), vecLatGrid(latGround.count),
        vecLonGrid(lonGround.count), vecDepth(verticesGround);

    //************* read ncdata into float-pointer *************//
    {
        // read bathymetry
        {
            const std::vector<size_t> vecStartBathy{latGround.start, lonGround.start};
            const std::vector<size_t> vecCountBathy{latGround.count, lonGround.count};
            bathymetryvar.getVar(vecStartBathy, vecCountBathy, vecDepth.data());
        }

        // read eta
        {
            vecEta.resize(nTimesteps * verticesSea);
            const std::vector<size_t> vecStartEta{firstTimestep, latSea.start, lonSea.start};
            const std::vector<size_t> vecCountEta{nTimesteps, latSea.count, lonSea.count};
            const std::vector<ptrdiff_t> vecStrideEta{incrementTimestep, latSea.stride, lonSea.stride};
            if (seaTimeConn) {
                eta.getVar(vecStartEta, vecCountEta, vecStrideEta, vecEta.data());

                //filter fillvalue
                if (m_fill->getValue()) {
                    const float &fillValNew = getFloatParameter("fillValueNew");
                    const float &fillVal = getFloatParameter("fillValue");
                    //TODO: Bad! needs rework.
                    std::replace(vecEta.begin(), vecEta.end(), fillVal, fillValNew);
                }
            }
        }

        // read lat, lon, grid_lat, grid_lon
        latSea.readNcVar(vecLat.data());
        lonSea.readNcVar(vecLon.data());
        latGround.readNcVar(vecLatGrid.data());
        lonGround.readNcVar(vecLonGrid.data());
    }

    //************* create Polygons ************//
    {
        std::vector<float *> coords{vecLat.data(), vecLon.data()};

        //************* create sea *************//
        {
            const auto &seaDim = Dim<size_t>(latSea.count, lonSea.count);
            const auto &polyDataSea = PolygonData<size_t>(numPolySea, numPolySea * 4, verticesSea);
            ptr_sea = generateSurface(polyDataSea, seaDim, coords);
        }

        //************* create grnd *************//
        coords[0] = vecLatGrid.data();
        coords[1] = vecLonGrid.data();

        const auto &scale = m_verticalScale->getValue();
        const auto &grndDim = Dim<size_t>(latGround.count, lonGround.count);
        const auto &polyDataGround = PolygonData<size_t>(numPolyGround, numPolyGround * 4, verticesGround);
        auto grndZCalc = [&vecDepth, &lonGround, &scale](size_t j, size_t k) {
            return -vecDepth[j * lonGround.count + k] * scale;
        };
        auto ptr_grnd = generateSurface(polyDataGround, grndDim, coords, grndZCalc);

        //************* create selected scalars *************//
        const std::vector<size_t> vecScalarStart{latSea.start, lonSea.start};
        const std::vector<size_t> vecScalarCount{latSea.count, lonSea.count};
        std::vector<float> vecScalar(verticesGround);
        for (size_t i = 0; i < NUM_SCALARS; ++i) {
            if (!m_scalarsOut[i]->isConnected())
                continue;
            const auto &scName = m_scalars[i]->getValue();
            const auto &val = ncFile->getVar(scName);
            Vec<Scalar>::ptr ptr_scalar(new Vec<Scalar>(verticesSea));
            ptr_Scalar[i] = ptr_scalar;
            auto scX = ptr_scalar->x().data();
            val.getVar(vecScalarStart, vecScalarCount, scX);

            //set some meta data
            ptr_scalar->addAttribute("_species", scName);
            ptr_scalar->setTimestep(-1);
            ptr_scalar->setBlock(blockNum);
        }

        // add ground data to port
        if (m_groundSurface_out->isConnected()) {
            ptr_grnd->setBlock(blockNum);
            ptr_grnd->setTimestep(-1);
            ptr_grnd->updateInternals();
            token.addObject(m_groundSurface_out, ptr_grnd);
        }
    }

    /* ncFile->close(); */
    return true;
}

/**
  * @brief Generates polygon for corresponding timestep and adds Object to scene.
  *
  * @token: Ref to internal vistle token.
  * @blockNum: current block number of parallel process.
  * @timestep: current timestep.
  * @return: true. TODO: add proper error-handling here.
  */
template<class T, class U>
bool ReadTsunami::computeTimestep(Token &token, const T &blockNum, const U &timestep)
{
    Polygons::ptr ptr_timestepPoly = ptr_sea->clone();
    static int indexEta{0};

    ptr_timestepPoly->resetArrays();

    // reuse data from sea polygon surface and calculate z new
    ptr_timestepPoly->d()->x[0] = ptr_sea->d()->x[0];
    ptr_timestepPoly->d()->x[1] = ptr_sea->d()->x[1];
    ptr_timestepPoly->d()->x[2].construct(ptr_timestepPoly->getSize());

    // getting z from vecEta and copy to z()
    // verticesSea * timesteps = total count vecEta
    auto startCopy = vecEta.begin() + (indexEta++ * verticesSea);
    std::copy_n(startCopy, verticesSea, ptr_timestepPoly->z().begin());
    ptr_timestepPoly->updateInternals();
    ptr_timestepPoly->setTimestep(timestep);
    ptr_timestepPoly->setBlock(blockNum);

    if (m_seaSurface_out->isConnected())
        token.addObject(m_seaSurface_out, ptr_timestepPoly);

    //add scalar to ports
    for (size_t i = 0; i < NUM_SCALARS; ++i) {
        if (!m_scalarsOut[i]->isConnected())
            continue;

        auto scalar = ptr_Scalar[i]->clone();
        scalar->setGrid(ptr_timestepPoly);
        scalar->addAttribute("_species", scalar->getAttribute("_species"));
        scalar->setBlock(blockNum);
        scalar->setTimestep(timestep);
        scalar->updateInternals();

        token.addObject(m_scalarsOut[i], scalar);
    }

    if (timestep == m_actualLastTimestep) {
        sendInfo("Cleared Cache for rank: " + std::to_string(rank()));
        std::vector<float>().swap(vecEta);
        for (auto &val: ptr_Scalar)
            val.reset();
        indexEta = 0;
    }
    return true;
}

bool ReadTsunami::finishRead()
{
    ncFile->close();
    return true;
}
