/******************************************************************************
 * THE OMEGA LIB PROJECT
 *-----------------------------------------------------------------------------
 * Copyright 2010-2015		Electronic Visualization Laboratory, 
 *							University of Illinois at Chicago
 * Authors:										
 *  Alessandro Febretti		febret@gmail.com
 *-----------------------------------------------------------------------------
 * Copyright (c) 2010-2015, Electronic Visualization Laboratory,  
 * University of Illinois at Chicago
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * Redistributions of source code must retain the above copyright notice, this 
 * list of conditions and the following disclaimer. Redistributions in binary 
 * form must reproduce the above copyright notice, this list of conditions and 
 * the following disclaimer in the documentation and/or other materials provided 
 * with the distribution. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR 
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *-----------------------------------------------------------------------------
 * What's in this file
 *	A cluster display system implementation based on the Equalizer parallel
 *  rendering framework.
 *  NOTE: This class only implements display configuration / startup / shutdown.
 *  additional classes are in the eqinternal directory
 ******************************************************************************/
#include <omegaGl.h>

#include "eqinternal.h"

#include "EqualizerDisplaySystem.h"
#include "omega/SystemManager.h"

using namespace omega;
using namespace co::base;
using namespace std;

#ifndef OMEGA_OS_WIN
#include <sys/stat.h>
#endif

#define OMEGA_EQ_TMP_FILE "./.eqcfg.eqc"

#define L(line) indent + line + "\n"
#define START_BLOCK(string, name) string += indent + name + "\n" + indent + "{\n"; indent += "\t";
#define END_BLOCK(string) indent = indent.substr(0, indent.length() - 1); string += indent + "}\n";

// for getenv(), used to read the DISPLAY env variable
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////////////////////////
void EqualizerSharedOStream::write(const void* data, uint64_t size)
{
    myStream->write(data, size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
SharedOStream& EqualizerSharedOStream::operator<< (const String& str)
{
    const uint64_t nElems = str.length();
    write(&nElems, sizeof(nElems));
    if (nElems > 0)
        write(str.c_str(), nElems);


    return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void EqualizerSharedIStream::read(void* data, uint64_t size)
{
    myStream->read(data, size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
SharedIStream& EqualizerSharedIStream::operator>> (String& str)
{
    uint64_t nElems = 0;
    read(&nElems, sizeof(nElems));
    if (nElems > myStream->getRemainingBufferSize())
    {
        oferror("SharedDataServices: nElems(%1%) > getRemainingBufferSize(%2%)",
            %nElems %myStream->getRemainingBufferSize());
    }
    oassert(nElems <= myStream->getRemainingBufferSize());
    if (nElems == 0)
        str.clear();
    else
    {
        str.assign(static_cast< const char* >(myStream->getRemainingBuffer()),
            nElems);
        myStream->advanceBuffer(nElems);
    }
    return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void SharedData::registerObject(SharedObject* module, const String& sharedId)
{
    //ofmsg("SharedData::registerObject: registering %1%", %sharedId);
    myObjects[sharedId] = module;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void SharedData::unregisterObject(const String& sharedId)
{
    //ofmsg("SharedData::unregisterObject: unregistering %1%", %sharedId);
    myObjectsToUnregister.push_back(sharedId);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void SharedData::getInstanceData(co::DataOStream& os)
{
    //omsg("#### SharedData::getInstanceData");
    EqualizerSharedOStream eos(&os);
    SharedOStream& out = eos;

    // Serialize update context.
    out << myUpdateContext.frameNum << myUpdateContext.dt << myUpdateContext.time;

    int numObjects = myObjects.size();
    out << numObjects;

    foreach(SharedObjectItem obj, myObjects)
    {
        out << obj.getKey();
        obj->commitSharedData(out);
    }

    foreach(String id, myObjectsToUnregister) myObjects.erase(id);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
void SharedData::applyInstanceData(co::DataIStream& is)
{
    //omsg("#### SharedData::applyInstanceData");
    EqualizerSharedIStream eis(&is);
    SharedIStream& in = eis;

    // Desrialize update context.
    in >> myUpdateContext.frameNum >> myUpdateContext.dt >> myUpdateContext.time;

    int numObjects;
    in >> numObjects;

    while (numObjects > 0)
    {
        String objId;
        in >> objId;

        SharedObject* obj = myObjects[objId];
        if (obj != NULL)
        {
            obj->updateSharedData(in);
        }
        else
        {
            oferror("FATAL ERROR: SharedDataServices::applyInstanceData: could not find object key %1%", %objId);
        }

        numObjects--;
    };
}

///////////////////////////////////////////////////////////////////////////////
void exitConfig()
{
    EqualizerDisplaySystem* ds = (EqualizerDisplaySystem*)SystemManager::instance()->getDisplaySystem();
    ds->exitConfig();
}

///////////////////////////////////////////////////////////////////////////////
EqualizerDisplaySystem::EqualizerDisplaySystem():
    mySys(NULL),
    myConfig(NULL),
    myNodeFactory(NULL),
    myDebugMouse(false)
{
}

///////////////////////////////////////////////////////////////////////////////
EqualizerDisplaySystem::~EqualizerDisplaySystem()
{
}
///////////////////////////////////////////////////////////////////////////////
void EqualizerDisplaySystem::exitConfig()
{
    SystemManager::instance()->postExitRequest();
}

///////////////////////////////////////////////////////////////////////////////
void EqualizerDisplaySystem::generateEqConfig()
{
    DisplayConfig& eqcfg = myDisplayConfig;
    String indent = "";

    String result = L("#Equalizer 1.0 ascii");

    START_BLOCK(result, "global");

    result += 
        L("EQ_CONFIG_FATTR_EYE_BASE 0.06") +
        L("EQ_WINDOW_IATTR_PLANES_STENCIL ON");
        //L("EQ WINDOW IATTR HINT SWAPSYNC OFF");

    END_BLOCK(result);

    START_BLOCK(result, "server");

    START_BLOCK(result, "connection");
    result +=
        L("type TCPIP") +
        L(ostr("port %1%", %eqcfg.basePort));
    END_BLOCK(result);
    
    START_BLOCK(result, "config");
    // Latency > 0 makes everything explode when a local node is initialized, due to 
    // multiple shared data messages sent to slave nodes before they initialize their local objects
    result += L(ostr("latency %1%", %eqcfg.latency));

    // Get the display port for the DISPLAY env variable, if present.
    int displayPort = 0;
    char* DISPLAY = getenv("DISPLAY");
    if(DISPLAY != NULL)
    {
        // given a variable like "blah.com:X.Y" we want to get X.
        Vector<String> a1 = StringUtils::split(DISPLAY, ":");
        Vector<String> a2 = StringUtils::split(a1.size() > 1 ? a1[1] : a1[0], ".");
        try
        {
            displayPort = boost::lexical_cast<int>(a2[0]);
        }
        catch(...)
        {
            ofwarn("DISPLAY env wrong format %1%", %DISPLAY);
        }
    }

    for(int n = 0; n < eqcfg.numNodes; n++)
    {
        DisplayNodeConfig& nc = eqcfg.nodes[n];
        // If all tiles are disabled for this node, skip it.
        if(!nc.enabled) continue;

        if(nc.isRemote)
        {
            int port = eqcfg.basePort + nc.port;
            START_BLOCK(result, "node");
            START_BLOCK(result, "connection");
            result +=
                L("type TCPIP") +
                L("hostname \"" + nc.hostname + "\"") +
                L(ostr("port %1%", %port));
            END_BLOCK(result);
            START_BLOCK(result, "attributes");
            result +=L("thread_model DRAW_SYNC");
            END_BLOCK(result);
        }
        else
        {
            START_BLOCK(result, "appNode");
            result += L("attributes { thread_model DRAW_SYNC }");
        }


        int winX = eqcfg.windowOffset[0];
        int winY = eqcfg.windowOffset[1];

        int curDevice = -1;

        // Write pipes section
        for(int i = 0; i < nc.numTiles; i++)
        {
            DisplayTileConfig& tc = *nc.tiles[i];
            //if(tc.enabled)
            {
                winX = tc.position[0] + eqcfg.windowOffset[0];
                winY = tc.position[1] + eqcfg.windowOffset[1];
            
                String tileName = tc.name;
                String tileCfg = buildTileConfig(
                    indent, 
                    tileName, 
                    winX, winY, 
                    tc.pixelSize[0], tc.pixelSize[1], 
                    displayPort, tc.device, curDevice, 
                    eqcfg.fullscreen, tc.borderless, tc.offscreen);

                result += tileCfg;

                curDevice = tc.device;
            }
        }

        if(curDevice != -1)
        {		
            END_BLOCK(result); // End last open pipe section
        }

        // end of node
        END_BLOCK(result);
    }

    typedef pair<String, DisplayTileConfig*> TileIterator;

    // compounds
    START_BLOCK(result, "compound")
    foreach(TileIterator p, eqcfg.tiles)
    {
        DisplayTileConfig* tc = p.second;
        if(tc->node && tc->node->enabled)
        {
            if(eqcfg.enableSwapSync)
            {
                //String tileCfg = ostr("\t\tcompound { swapbarrier { name \"defaultbarrier\" } channel ( canvas \"canvas-%1%\" segment \"segment-%2%\" layout \"layout-%3%\" view \"view-%4%\" ) }\n",
                String tileCfg = ostr("\t\tcompound { swapbarrier { name \"defaultbarrier\" } channel \"%1%\" task [DRAW]\n",	%tc->name);
                START_BLOCK(tileCfg, "wall");
                tileCfg +=
                    L("bottom_left [ -1 -0.5 0 ]") +
                    L("bottom_right [ 1 -0.5 0 ]") +
                    L("top_left [ -1 0.5 0 ]");
                END_BLOCK(tileCfg)
                result += tileCfg + "}\n";
            }
            else
            {
                String tileCfg = ostr("\t\tchannel \"%1%\" task [DRAW]\n", %tc->name);
                START_BLOCK(tileCfg, "wall");
                tileCfg +=
                    L("bottom_left [ -1 -0.5 0 ]") +
                    L("bottom_right [ 1 -0.5 0 ]") +
                    L("top_left [ -1 0.5 0 ]");
                END_BLOCK(tileCfg)
                result += tileCfg;
            }
        }
    }

    END_BLOCK(result)
    // ------------------------------------------ END compounds

    // end config
    END_BLOCK(result)

    // end server
    END_BLOCK(result)

    if(!eqcfg.disableConfigGenerator)
    {
        FILE* f = fopen(OMEGA_EQ_TMP_FILE, "w");
        if(f)
        {
            fputs(result.c_str(), f);
            fclose(f);
#ifndef OMEGA_OS_WIN
            // change file permissions so everyone can overwrite it.
            chmod(OMEGA_EQ_TMP_FILE, S_IRWXU | S_IRWXG | S_IRWXO);
#endif            
        }
        else
        {
            oerror("EqualizerDisplaySystem FATAL: could not create configuration file " OMEGA_EQ_TMP_FILE " - check for write permissions");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
String EqualizerDisplaySystem::buildTileConfig(String& indent, const String tileName, int x, int y, int width, int height, int port, int device, int curdevice, bool fullscreen, bool borderless, bool offscreen)
{
    String viewport = ostr("viewport [%1% %2% %3% %4%]", %x %y %width %height);

    String tileCfg = "";
    if(device != curdevice)
    {
        if(curdevice != -1) { END_BLOCK(tileCfg); } // End previous pipe section
        
        // Start new pipe section
        START_BLOCK(tileCfg, "pipe");
            tileCfg +=
                L(ostr("name = \"%1%-%2%\"", %tileName %device)) +
                L(ostr("port = %1%", %port)) +
                L(ostr("device = %1%", %device));
    }
    START_BLOCK(tileCfg, "window");
    tileCfg +=
        L("name \"" + tileName + "\"") +
        L(viewport) +
        L("channel { name \"" + tileName + "\"}");
    if(fullscreen)
    {
        START_BLOCK(tileCfg, "attributes");
        tileCfg +=
            L("hint_fullscreen ON") +
            L("hint_decoration OFF");
        END_BLOCK(tileCfg);
    }
    else if(borderless)
    {
        START_BLOCK(tileCfg, "attributes");
        tileCfg +=
            L("hint_decoration OFF");
        END_BLOCK(tileCfg);
    }
    else if(offscreen)
    {
        START_BLOCK(tileCfg, "attributes");
        tileCfg +=
            L("hint_drawable FBO");
        END_BLOCK(tileCfg);
    }
    END_BLOCK(tileCfg)
    return tileCfg;
}

///////////////////////////////////////////////////////////////////////////////
void EqualizerDisplaySystem::setupEqInitArgs(int& numArgs, const char** argv)
{
    SystemManager* sys = SystemManager::instance();
    const char* appName = sys->getApplication()->getName();
    if(SystemManager::instance()->isMaster())
    {
        argv[0] = appName;
        argv[1] = "--eq-config";
        argv[2] = OMEGA_EQ_TMP_FILE;
        numArgs = 3;
    }
    else
    {
        argv[0] = appName;
        argv[1] = "--eq-client";
        argv[2] = "--eq-listen";
        argv[3] = SystemManager::instance()->getHostnameAndPort().c_str();
        numArgs = 4;
    }
}

///////////////////////////////////////////////////////////////////////////////
void EqualizerDisplaySystem::initialize(SystemManager* sys)
{
    if(getDisplayConfig().verbose) 	Log::level = LOG_INFO;
    else Log::level = LOG_WARN;
    mySys = sys;

    //atexit(::exitConfig);

    // Launch application instances on secondary nodes.
    if(SystemManager::instance()->isMaster())
    {
        // Generate the equalizer configuration
        generateEqConfig();
        
        for(int n = 0; n < myDisplayConfig.numNodes; n++)
        {
            DisplayNodeConfig& nc = myDisplayConfig.nodes[n];

            if(nc.hostname != "local" && nc.enabled)
            {
                String executable = StringUtils::replaceAll(myDisplayConfig.nodeLauncher, "%c", SystemManager::instance()->getApplication()->getExecutableName());
                executable = StringUtils::replaceAll(executable, "%h", nc.hostname);
            
                // Substitute %d with current working directory
                String cCurrentPath = ogetcwd();
                executable = StringUtils::replaceAll(executable, "%d", cCurrentPath);
            
                // Setup the executable call. Note: we pass a-D argument to tell all
                // instances what the main data directory is. We use ogetdataprefix
                // because omain sets the data prefix to the root data dir during
                // startup.
                int port = myDisplayConfig.basePort + nc.port;
                
                const Rect& ic = myDisplayConfig.getCanvasRect();
                String initialCanvas = ostr("%1%,%2%,%3%,%4%", %ic.x() %ic.y() %ic.width() %ic.height());
                
                String cmd = ostr("%1% -c %2%@%3%:%4% -D %5% -w %6%", 
                    %executable 
                    %SystemManager::instance()->getAppConfig()->getFilename() 
                    %nc.hostname 
                    %port 
                    %ogetdataprefix() 
                    %initialCanvas);
                olaunch(cmd);
            }
        }
        osleep(myDisplayConfig.launcherInterval);
    }
}

///////////////////////////////////////////////////////////////////////////////
void EqualizerDisplaySystem::killCluster() 
{
    olog(Verbose, "EqualizerDisplaySystem::killCluster");
    
    // Get process name from application executable.
    String execname = SystemManager::instance()->getApplication()->getExecutableName();
    String procName;
    String ext;
    String dir;
    StringUtils::splitFullFilename(execname, procName, ext, dir);
 
    if(SystemManager::instance()->isMaster())
    {
        ofmsg("number of nodes: %1%", %myDisplayConfig.numNodes);
        for(int n = 0; n < myDisplayConfig.numNodes; n++)
        {
            DisplayNodeConfig& nc = myDisplayConfig.nodes[n];
            
            if(nc.hostname != "local" && nc.enabled && myDisplayConfig.nodeKiller != "")
            {
                String executable = StringUtils::replaceAll(myDisplayConfig.nodeKiller, "%c", procName);
                executable = StringUtils::replaceAll(executable, "%h", nc.hostname);
                olaunch(executable);
            }
        }
    }
    
    // kindof hack but it works: kill master instance.
    olaunch(ostr("killall %1%", %procName));
}

///////////////////////////////////////////////////////////////////////////////
void EqualizerDisplaySystem::finishInitialize(ConfigImpl* config, Engine* engine)
{
    myConfig = config;
    // Setup cameras for each tile.
    typedef KeyValue<String, Ref<DisplayTileConfig> > TileItem;
    foreach(TileItem dtc, myDisplayConfig.tiles)
    {
        if(dtc->cameraName == "")
        {
            // Use default camera for this tile
            dtc->camera = engine->getDefaultCamera();
        }
        else
        {
            // Use a custom camera for this tile (create it here if necessary)
            Camera* customCamera = engine->getCamera(dtc->cameraName);
            if(customCamera == NULL)
            {
                customCamera = engine->createCamera(dtc->cameraName);
            }
            dtc->camera = customCamera;
        }	
    }
}

///////////////////////////////////////////////////////////////////////////////
void EqualizerDisplaySystem::run()
{
    bool error = false;
    const char* argv[4];
    int numArgs = 0;
    setupEqInitArgs(numArgs, (const char**)argv);
    myNodeFactory = new EqualizerNodeFactory();
    olog(Verbose, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> DISPLAY INITIALIZATION");
    if( !eq::init( numArgs, (char**)argv, myNodeFactory ))
    {
        oerror("Equalizer init failed");
    }
        
    myConfig = static_cast<ConfigImpl*>(eq::getConfig( numArgs, (char**)argv ));
    omsg("Equalizer display system initializing");
    
    // If this is the master node, run the master loop.
    if(myConfig && mySys->isMaster())
    {
        //olog(Verbose, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> DISPLAY INITIALIZATION");
        if( myConfig->init())
        {
            olog(Verbose, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< DISPLAY INITIALIZATION\n\n");
            olog(Verbose, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> APPLICATION LOOP");

            uint32_t spin = 0;
            bool exitRequestProcessed = false;
            while(!SystemManager::instance()->isExitRequested())
            {
                myConfig->startFrame( spin );
                myConfig->finishFrame();
                spin++;
                if(SystemManager::instance()->isExitRequested()
                    && !exitRequestProcessed)
                {
                    exitRequestProcessed = true;

                    // Run one additional frame, to give all omegalib objects
                    // a change to dispose correctly.
                    myConfig->startFrame( spin );
                    myConfig->finishAllFrames();
                }
            }
            olog(Verbose, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< APPLICATION LOOP\n\n");
        }
        else
        {
            oerror("Config initialization failed!");
            error = true;
        }
    }
    else
    {
        oerror("Cannot get config");
        error = true;
    }    
}

///////////////////////////////////////////////////////////////////////////////
void EqualizerDisplaySystem::cleanup()
{
    if(myConfig != NULL)
    {
        myConfig->exit();
        eq::releaseConfig( myConfig );
        eq::exit();
    }

    delete myNodeFactory;
    SharedDataServices::cleanup();
}
