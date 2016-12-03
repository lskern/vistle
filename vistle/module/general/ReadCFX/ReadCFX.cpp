#include <string>
#include <sstream>

#include <core/object.h>
#include <core/vec.h>
#include <core/polygons.h>
#include <core/triangles.h>
#include <core/lines.h>
#include <core/points.h>
#include <core/normals.h>

// Includes for the CFX application programming interface (API)
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cfxExport.h>  // linked but still qtcreator doesn't find it
#include <getargs.h>    // linked but still qtcreator doesn't find it
#include <iostream>
//#include <unistd.h>

//#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/cstdint.hpp>

#include "ReadCFX.h"

namespace bf = boost::filesystem;

MODULE_MAIN(ReadCFX)

using namespace vistle;


ReadCFX::ReadCFX(const std::string &shmname, const std::string &name, int moduleID)
   : Module("ReadCFX", shmname, name, moduleID) {

    // file browser parameter
    //m_resultfiledir = addStringParameter("resultfiledir", "CFX case directory","/mnt/raid/home/hpcjwint/data/cfx/rohr/", Parameter::Directory);
    m_resultfiledir = addStringParameter("resultfiledir", "CFX case directory","/home/jwinterstein/data/cfx/rohr/hlrs_002.res", Parameter::Directory);

    // time parameters
    m_starttime = addFloatParameter("starttime", "start reading at the first step after this time", 0.);
    setParameterMinimum<Float>(m_starttime, 0.);
    m_stoptime = addFloatParameter("stoptime", "stop reading at the last step before this time",
          std::numeric_limits<double>::max());
    setParameterMinimum<Float>(m_stoptime, 0.);
    m_timeskip = addIntParameter("timeskip", "skip this many timesteps after reading one", 0);
    setParameterMinimum<Integer>(m_timeskip, 0);
    m_readGrid = addIntParameter("read_grid", "load the grid?", 1, Parameter::Boolean);

    // mesh ports
    m_gridOut = createOutputPort("grid_out1");

    // data ports and data choice parameters
    for (int i=0; i<NumPorts; ++i) {
        {// data ports
            std::stringstream s;
            s << "data_out" << i;
            m_volumeDataOut.push_back(createOutputPort(s.str()));
        }
        {// data choice parameters
            std::stringstream s;
            s << "Data" << i;
            auto p =  addStringParameter(s.str(), "name of field", "(NONE)", Parameter::Choice);
            std::vector<std::string> choices;
            choices.push_back("(NONE)");
            setParameterChoices(p, choices);
            m_fieldOut.push_back(p);
        }
    }
    m_readBoundary = addIntParameter("read_boundary", "load the boundary?", 1, Parameter::Boolean);
    //m_boundaryPatchesAsVariants = addIntParameter("patches_as_variants", "create sub-objects with variant attribute for boundary patches", 1, Parameter::Boolean);
    //m_patchSelection = addStringParameter("patches", "select patches","all");

    // 2d data ports and 2d data choice parameters
    for (int i=0; i<NumBoundaryPorts; ++i) {
       {// 2d data ports
          std::stringstream s;
          s << "data_2d_out" << i;
          m_boundaryDataOut.push_back(createOutputPort(s.str()));
       }
       {// 2d data choice parameters
          std::stringstream s;
          s << "Data2d" << i;
          auto p =  addStringParameter(s.str(), "name of field", "(NONE)", Parameter::Choice);
          std::vector<std::string> choices;
          choices.push_back("(NONE)");
          setParameterChoices(p, choices);
          m_boundaryOut.push_back(p);
       }
    }
    //m_buildGhostcellsParam = addIntParameter("build_ghostcells", "whether to build ghost cells", 1, Parameter::Boolean);ll

    m_case = new CaseInfo();

   /*addIntParameter("indexed_geometry", "create indexed geometry?", 0, Parameter::Boolean);
   addIntParameter("triangulate", "only create triangles", 0, Parameter::Boolean);

   addIntParameter("first_block", "number of first block", 0);
   addIntParameter("last_block", "number of last block", 0);

   addIntParameter("first_step", "number of first timestep", 0);
   addIntParameter("last_step", "number of last timestep", 0);
   addIntParameter("step", "increment", 1);

   addIntParameter("ignore_errors", "ignore files that are not found", 0, Parameter::Boolean);
*/
}

ReadCFX::~ReadCFX() {

}

int CaseInfo::checkFile(const char *filename) {
    const int MIN_FILE_SIZE = 1024; // minimal size for .res files [in Byte]

    const int MACIC_LEN = 5; // "magic" at the start
    const char *magic = "*INFO";
    char magicBuf[MACIC_LEN];

    boost::uintmax_t fileSize;
    boost::system::error_code ec;

    FILE *fi = fopen(filename, "r");

    if (!fi) {
        std::cout << filename << strerror(errno) << std::endl;
        return -1;
    }
    else {
        fileSize = bf::file_size(filename, ec);
        if (ec)
            std::cout << "error code: " << ec << std::endl;
    }

    if (fileSize < MIN_FILE_SIZE) {
        std::cout << filename << "too small to be a real result file" << std::endl;
        std::cout << fileSize << "filesize" << std::endl;
        return -2;
    }

    size_t iret = fread(magicBuf, 1, MACIC_LEN, fi);
    if (iret != MACIC_LEN) {
        std::cout << "checkFile :: error reading MACIC_LEN " << std::endl;
        return -3;
    }

    if (strncasecmp(magicBuf, magic, MACIC_LEN) != 0) {
        std::cout << filename << "does not start with '*INFO'" << std::endl;
        return -4;
    }

    fclose(fi);

    return 0;
}

void CaseInfo::checkFields(std::map<int, std::string> &field_param, std::map<int, std::string> &boundary_param) {

    int usr_level = 0;
    int i=0, j=0;
    int dimension, corrected_boundary_node;
    int nnodes = cfxExportNodeCount();

    int nvars = cfxExportVariableCount(usr_level);
    //sendInfo("nvars = %d",nvars);

    for(int varnum=1;varnum<=nvars;varnum++) {
        if(cfxExportVariableSize(varnum,&dimension,&nnodes,&corrected_boundary_node)) { //cfxExportVariableSize returns 1 if successful or 0 if the variable is out of range
            if(nnodes != 1) { //field parameter
                field_param[i]=cfxExportVariableName(varnum,1); //0 is short form and 1 is long form of the variable name
                i++;
            }
        }
    }

    for(int varnum=1;varnum<=nvars;varnum++) {
        if(cfxExportVariableSize(varnum,&dimension,&nnodes,&corrected_boundary_node)) { //cfxExportVariableSize returns 1 if successful or 0 if the variable is out of range
            if(nnodes == 1) { //boundary parameter
                boundary_param[j]=cfxExportVariableName(varnum,1); //0 is short form and 1 is long form of the variable name
                j++;
            }
        }
    }
}

void CaseInfo::getCaseInfo(const std::string &resultfiledir) {
    const char *c_resultfiledir;
    c_resultfiledir = resultfiledir.c_str();

    m_valid = checkFile(c_resultfiledir);
    checkFields(m_field_param, m_boundary_param);

    return;
}



std::vector<std::string> ReadCFX::getFieldList() const {

   std::vector<std::string> choices;
   choices.push_back("(NONE)");

   if (m_case.m_valid) {
      for (auto &field: m_case.m_field_param)
         choices.push_back(field.second);
      for (auto &field: m_case.m_boundary_param)
         choices.push_back(field.second);
   }

   return choices;
}

/*int ReadFOAM::rankForBlock(int processor) const {

   if (m_case.numblocks == 0)
      return 0;

   if (processor == -1)
      return -1;

   return processor % size();
}*/


/*int ReadCFX::rankForBlock(int block) const {

    const int numBlocks = m_lastBlock-m_firstBlock+1;
    if (numBlocks == 0)
        return 0;

    if (block == -1)
        return -1;

    return block % size();
}*/


bool ReadCFX::parameterChanged(const Parameter *p) {
    auto sp = dynamic_cast<const StringParameter *>(p);
    if (sp == m_resultfiledir) {
//        std::string c = sp->getValue();
//        const char *resultfiledir;
//        resultfiledir = c.c_str();


        m_case.CaseInfo::getCaseInfo(m_resultfiledir);
        if (!m_case.m_valid) {
           std::cerr << m_resultfiledir << " is not a valid CFX .res file" << std::endl;
           return false;
        }
        else {
//            static char *resultName = NULL;
//            std::cerr << "resultfiledir = " << resultfiledir << std::endl;
//            std::cerr << "resultName = " << resultName << std::endl;

//            if (resultName == NULL || strcmp(resultName, resultfiledir) != 0) {
//                resultName = new char[strlen(resultfiledir) + 1];
//                strcpy(resultName, resultfiledir);

                sendInfo("Please wait...");

                if (nzones > 0)
                    cfxExportDone();

                nzones = cfxExportInit(resultfiledir, NULL);
                /*nzones = cfxExportInit(resultName, counts);

                for(i=0;i<cfxCNT_SIZE;i++) {
                    std::cerr << "counts[" << i << "] = " << counts[i] << std::endl;
                }*/

                /*std::cerr << "nzones: " << nzones << std::endl;
                std::cerr << "cfxCNT_SIZE: " << cfxCNT_SIZE << std::endl;

                for(int i=0;i<nzones;i++) {
                    std::cerr << "cfxExportZoneSet(): " << cfxExportZoneSet(i,NULL) << std::endl;
                    std::cerr << "cfxExportZoneGet(): " << cfxExportZoneGet() << std::endl;
                    std::cerr << "cfxExportTimestepCount(): " << cfxExportTimestepCount() << std::endl;
                    std::cerr << "cfxExportTimestepNumGet(1): " << cfxExportTimestepNumGet(1) << std::endl;
                    std::cerr << "cfxExportTimestepTimeGet(1): " << cfxExportTimestepTimeGet(1) << std::endl;
                    std::cerr << "cfxExportNodeCount(): " << cfxExportNodeCount() << std::endl;
                    std::cerr << "cfxExportElementCount(): " << cfxExportElementCount() << std::endl;
                    std::cerr << "cfxExportZoneGet(): " << cfxExportZoneGet() << std::endl;
                    std::cerr << "cfxExportZoneCount(): " << cfxExportZoneCount() << std::endl;
                    std::cerr << "cfxExportRegionCount(): " << cfxExportRegionCount() << std::endl;
                    std::cerr << "cfxExportVolumeCount(): " << cfxExportVolumeCount() << std::endl;
                    std::cerr << "cfxExportBoundaryCount(): " << cfxExportBoundaryCount() << std::endl << std::endl;
                }*/

                timeStepNum = cfxExportTimestepNumGet(1);
                if (timeStepNum < 0) {
                    sendInfo("no timesteps");
                }

                iteration = cfxExportTimestepNumGet(1);
                if (cfxExportTimestepSet(iteration) < 0)
                {
                    sendInfo("Invalid timestep %d", iteration);
                }

                sendInfo("Found %d zones", nzones);

                // @@@ cfxExportDone();
                sendInfo("The initialisation was successfully done");

            }

            //fill choice parameters
            std::vector<std::string> choices = getFieldList();
            std::vector<std::string>::iterator it;
            for(it=choices.begin();it!=choices.end();it++) {
                sendInfo("choices = %s \n",it->c_str());
            }
            //const std::vector<std::string> choices = {"none1","none2","none3"};

            for (auto out: m_fieldOut) {
               setParameterChoices(out, choices);
            }
            for (auto out: m_boundaryOut) {
               setParameterChoices(out, choices);
            }
        }
    }


    return Module::parameterChanged(p);
}

bool ReadCFX::compute() {

    std::cerr << "Compute Start. \n";


    //write nodes

    nnodes = cfxExportNodeCount();
    std::cerr << "nnodes = " << nnodes << std::endl;

    nodes = cfxExportNodeList();

    for(int n=0;n<10;n++,nodes++) {

        std::cerr << "x = " << nodes->x << " y = " << nodes->y << " z = " << nodes->z << std::endl;
    }

    cfxExportNodeFree();

    //write elements
    nelems = cfxExportElementCount();
    std::cerr << "nelems = " << nelems << std::endl;

    if (counts[cfxCNT_TET]) {
           elems = cfxExportElementList();
           for (int n = 0; n < 1; n++, elems++) {
               if (cfxELEM_TET == elems->type) {
                   for (int i = 0; i < elems->type; i++)
                       std::cerr << "elems = " << elems->nodeid[i] << std::endl;
               }
           }
       }



    cfxExportElementFree();



 /*  m_firstBlock = getIntParameter("first_block");
   m_lastBlock = getIntParameter("last_block");
   m_firstStep = getIntParameter("first_step");
   m_lastStep = getIntParameter("last_step");
   m_step = getIntParameter("step");
   if ((m_lastStep-m_firstStep)*m_step < 0)
       m_step *= -1;
   bool reverse = false;
   int step = m_step, first = m_firstStep, last = m_lastStep;
   if (step < 0) {
       reverse = true;
       step *= -1;
       first *= -1;
       last *= -1;
   }

   std::string filename = getStringParameter("filename");

   int timeCounter = 0;
   for (int t=first; t<=last; t += step) {
       int timestep = reverse ? -t : t;
       bool loaded = false;
       for (int b=m_firstBlock; b<=m_lastBlock; ++b) {
           if (rankForBlock(b) == rank()) {
               std::string f;
               try {
                   using namespace boost::io;
                   boost::format fmter(filename);
                   fmter.exceptions(all_error_bits ^ (too_many_args_bit | too_few_args_bit));
                   fmter % b;
                   fmter % timestep;
                   f = fmter.str();
               } catch (boost::io::bad_format_string except) {
                   sendError("bad format string in filename");
                   return true;
               }
               auto obj = load(f);
               if (!obj) {
                   if (!getIntParameter("ignore_errors")) {
                       sendError("failed to load %s", f.c_str());
                       return true;
                   }
               } else {
                   obj->setBlock(b);
                   obj->setTimestep(timeCounter);
                   loaded = true;
                   addObject("grid_out", obj);
               }
           }
       }
       if (loaded)
           ++timeCounter;
   }*/

   return true;
}
