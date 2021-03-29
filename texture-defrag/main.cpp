/*******************************************************************************
    Copyright (c) 2021, Andrea Maggiordomo, Paolo Cignoni and Marco Tarini

    This file is part of TextureDefrag, a reference implementation for
    the paper ``Texture Defragmentation for Photo-Reconstructed 3D Models''
    by Andrea Maggiordomo, Paolo Cignoni and Marco Tarini.

    TextureDefrag is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    TextureDefrag is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TextureDefrag. If not, see <https://www.gnu.org/licenses/>.
*******************************************************************************/

#include "mesh.h"

#include "timer.h"
#include "texture_object.h"
#include "texture_optimization.h"
#include "packing.h"
#include "logging.h"
#include "utils.h"
#include "mesh_attribute.h"
#include "seam_remover.h"
#include "texture_rendering.h"

#include <wrap/io_trimesh/io_mask.h>
#include <wrap/system/qgetopt.h>

#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/update/color.h>

#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#include <QApplication>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QString>


struct Args {
    double m = 2.0;
    double b = 0.2;
    double d = 0.5;
    double g = 0.025;
    double u = 0.0;
    double a = 5.0;
    double t = 0.0;
    std::string infile = "";
    std::string outfile = "";
    int l = 0;
};

void PrintArgsUsage(const char *binary);
bool ParseOption(const std::string& option, const std::string& argument, Args *args);
Args ParseArgs(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    // Make sure the executable directory is added to Qt's library path
    QApplication app(argc, argv);

    AlgoParameters ap;

    Args args = ParseArgs(argc, argv);

    ap.matchingThreshold = args.m;
    ap.boundaryTolerance = args.b;
    ap.distortionTolerance = args.d;
    ap.globalDistortionThreshold = args.g;
    ap.UVBorderLengthReduction = args.u;
    ap.offsetFactor = args.a;
    ap.timelimit = args.t;

    LOG_INIT(args.l);

    Mesh m;
    TextureObjectHandle textureObject;
    int loadMask;

    Timer t;

    if (LoadMesh(args.infile.c_str(), m, textureObject, loadMask) == false) {
        LOG_ERR << "Failed to open mesh";
        std::exit(-1);
    }

    ensure(loadMask & tri::io::Mask::IOM_WEDGTEXCOORD);
    tri::UpdateTopology<Mesh>::FaceFace(m);

    tri::UpdateNormal<Mesh>::PerFaceNormalized(m);
    tri::UpdateNormal<Mesh>::PerVertexNormalized(m);

    ScaleTextureCoordinatesToImage(m, textureObject);

    LOG_VERBOSE << "Preparing mesh...";

    int vndupIn;
    PrepareMesh(m, &vndupIn);
    ComputeWedgeTexCoordStorageAttribute(m);

    GraphHandle graph = ComputeGraph(m, textureObject);

    std::map<RegionID, bool> flipped;
    for (auto& c : graph->charts)
        flipped[c.first] = c.second->UVFlipped();

    double inputMP = textureObject->GetResolutionInMegaPixels();
    int inputCharts = graph->Count();
    double inputUVLen = graph->BorderUV();

    // ensure all charts are oriented coherently, and then store the wtc attribute
    ReorientCharts(graph);

    std::map<ChartHandle, int> anchorMap;
    AlgoStateHandle state = InitializeState(graph, ap);

    GreedyOptimization(graph, state, ap);

    int vndupOut;

    Finalize(graph, &vndupOut);

    double zeroResamplingFraction = 0;

    bool colorize = true;

    if (colorize)
        tri::UpdateColor<Mesh>::PerFaceConstant(m, vcg::Color4b(91, 130, 200, 255));

    LOG_INFO << "Rotating charts...";
    double zeroResamplingMeshArea = 0;
    for (auto entry : graph->charts) {
        ChartHandle chart = entry.second;
        double zeroResamplingChartArea;
        int anchor = RotateChartForResampling(chart, state->changeSet, flipped, colorize, &zeroResamplingChartArea);
        if (anchor != -1) {
            anchorMap[chart] = anchor;
            zeroResamplingMeshArea += zeroResamplingChartArea;
        }
    }
    zeroResamplingFraction = zeroResamplingMeshArea / graph->Area3D();

    int outputCharts = graph->Count();
    double outputUVLen = graph->BorderUV();

    // pack the atlas

    // first discard zero-area charts
    std::vector<ChartHandle> chartsToPack;
    for (auto& entry : graph->charts) {
        if (entry.second->AreaUV() != 0) {
            chartsToPack.push_back(entry.second);
        } else {
            for (auto fptr : entry.second->fpVec) {
                for (int j = 0; j < fptr->VN(); ++j) {
                    fptr->V(j)->T().P() = Point2d::Zero();
                    fptr->V(j)->T().N() = 0;
                    fptr->WT(j).P() = Point2d::Zero();
                    fptr->WT(j).N() = 0;
                }
            }
        }
    }

    LOG_INFO << "Packing atlas of size " << chartsToPack.size();

    Timer tp;
    std::vector<TextureSize> texszVec;
    int npacked = Pack(chartsToPack, textureObject, texszVec);

    LOG_INFO << "Packed " << npacked << " charts in " << tp.TimeElapsed() << " seconds";
    if (npacked < (int) chartsToPack.size()) {
        LOG_ERR << "Not all charts were packed (" << chartsToPack.size() << " charts, " << npacked << " packed)";
        std::exit(-1);
    }

    LOG_INFO << "Trimming texture...";

    TrimTexture(m, texszVec, false);

    LOG_INFO << "Shifting charts...";

    IntegerShift(m, chartsToPack, texszVec, anchorMap, flipped);

    LOG_INFO << "Rendering texture...";

    std::vector<std::shared_ptr<QImage>> newTextures = RenderTexture(m, textureObject, texszVec, true, RenderMode::Linear);

    double outputMP;
    {
        int64_t totArea = 0;
        for (unsigned i = 0; i < newTextures.size(); ++i) {
            totArea += newTextures[i]->width() * newTextures[i]->height();
        }
        outputMP = totArea / 1000000.0;
    }

    LOG_INFO << "InputVert " << m.VN();
    LOG_INFO << "InputVertDup " << vndupIn;
    LOG_INFO << "OutputVertDup " << vndupOut;
    LOG_INFO << "InputCharts " << inputCharts;
    LOG_INFO << "OutputCharts " << outputCharts;
    LOG_INFO << "InputUVLen " << inputUVLen;
    LOG_INFO << "OutputUVLen " << outputUVLen;
    LOG_INFO << "InputMP " << inputMP;
    LOG_INFO << "OutputMP " << outputMP;
    LOG_INFO << "RelativeMPChange " << ((outputMP - inputMP) / inputMP);
    LOG_INFO << "ZeroResamplingFraction " << zeroResamplingFraction;

    LOG_INFO << "Saving mesh file...";

    std::string savename = args.outfile;
    if (savename == "")
        savename = "out_" + m.name;
    if (savename.substr(savename.size() - 3, 3) == "fbx")
        savename.append(".obj");

    if (SaveMesh(savename.c_str(), m, newTextures, true) == false)
        LOG_ERR << "Model not saved correctly";

    LOG_INFO << "Processing took " << t.TimeElapsed() << " seconds";

    return 0;
}

void PrintArgsUsage(const char *binary) {
    Args def;
    std::cout << "Usage: " << binary << " MESHFILE [-mbdgutao]" << std::endl;
    std::cout << std::endl;
    std::cout << "MESHFILE specifies the input mesh file (supported formats are obj, ply and fbx)" << std::endl;
    std::cout << std::endl;
    std::cout << "-m  <val>      " << "Matching error tolerance when attempting merge operations." << " (default: " << def.m << ")" << std::endl;
    std::cout << "-b  <val>      " << "Maximum tolerance on the seam-length to chart-perimeter ratio when attempting merge operations. Range is [0,1]." << " (default: " << def.b << ")" << std::endl;
    std::cout << "-d  <val>      " << "Local ARAP distortion tolerance when performing the local UV optimization." << " (default: " << def.d << ")" << std::endl;
    std::cout << "-g  <val>      " << "Global ARAP distortion tolerance when performing the local UV optimization." << " (default: " << def.g << ")" << std::endl;
    std::cout << "-u  <val>      " << "UV border reduction target in percentage relative to the input. Range is [0,1]." << " (default: " << def.u << ")" << std::endl;
    std::cout << "-a  <val>      " << "Alpha parameter to control the UV optimization area size." << " (default: " << def.a << ")" << std::endl;
    std::cout << "-t  <val>      " << "Time-limit for the atlas clustering (in seconds)." << " (default: " << def.t << ")" << std::endl;
    std::cout << "-o  <val>      " << "Output mesh file. Supported formats are obj and ply." << " (default: out_MESHFILE" << ")" << std::endl;
    std::cout << "-l  <val>      " << "Logging level. 0 for minimal verbosity, 1 for verbose output, 2 for debug output." << " (default: " << def.l << ")" << std::endl;
}

bool ParseOption(const std::string& option, const std::string& argument, Args *args)
{
    ensure(option.size() == 2);
    if (option[1] == 'o') {
        args->outfile = argument;
        return true;
    }
    if (option[1] == 'l') {
        args->l = std::stoi(argument);
        if (args->l >= 0)
            return true;
        else {
            std::cerr << "Logging level must be positive" << std::endl << std::endl;
            return false;
        }
    }
    try {
        switch (option[1]) {
            case 'm': args->m = std::stod(argument); break;
            case 'b': args->b = std::stod(argument); break;
            case 'd': args->d = std::stod(argument); break;
            case 'g': args->g = std::stod(argument); break;
            case 'u': args->u = std::stod(argument); break;
            case 'a': args->a = std::stod(argument); break;
            case 't': args->t = std::stod(argument); break;
            default:
                std::cerr << "Unrecognized option " << option << std::endl << std::endl;
                return false;
        }
    } catch (std::exception e) {
        std::cerr << "Error while parsing option `" << option << " " << argument << "`" << std::endl << std::endl;;
        return false;
    }
    return true;
}

Args ParseArgs(int argc, char *argv[])
{
    if (argc < 2) {
        PrintArgsUsage(argv[0]);
        std::exit(-1);
    }

    Args args;

    for (int i = 0; i < argc; ++i) {
        std::string argi(argv[i]);
        if (argi[0] == '-' && argi.size() == 2) {
            i++;
            if (i >= argc) {
                std::cerr << "Missing argument for option " << argi << std::endl << std::endl;
                PrintArgsUsage(argv[0]);
                std::exit(-1);
            } else {
                if (!ParseOption(argi, std::string(argv[i]), &args)) {
                    PrintArgsUsage(argv[0]);
                    std::exit(-1);
                }
            }
        } else {
            args.infile = argi;
        }
    }

    if (args.infile == "") {
        std::cerr << "Missing input mesh argument" << std::endl << std::endl;
        PrintArgsUsage(argv[0]);
        std::exit(-1);
    }

    return args;
}

