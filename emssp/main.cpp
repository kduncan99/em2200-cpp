//  SSP command line utility for emulated exec

//???? Need /Call {file_name} command for executing scripts (needs to be stack-based, so scripts can invoke /call))



#include    "emssp.h"



//  local constants
#define     NODE_CONFIG_FILE_NAME       BASE_PATH "emssp_node.cfg"



//  typedefs

typedef     bool        (*COMMAND_HANDLER)( const std::list<SuperString>& arguments );



//  function prototypes

static bool     callHandler( const std::list<SuperString>& parameters );
static bool     execBootHandler( const std::list<SuperString>& parameters );
static bool     execConfigHandler( const std::list<SuperString>& parameters );
static bool     execDumpHandler( const std::list<SuperString>& parameters );
static bool     execHaltHandler( const std::list<SuperString>& parameters );
static bool     execLoadHandler( const std::list<SuperString>& parameters );
static bool     execReloadHandler( const std::list<SuperString>& parameters );
static bool     execStatusHandler( const std::list<SuperString>& parameters );
static bool     execUnloadHandler( const std::list<SuperString>& parameters );
static bool     helpHandler( const std::list<SuperString>& parameters );
static bool     jumpClearHandler( const std::list<SuperString>& parameters );
static bool     jumpSetHandler( const std::list<SuperString>& parameters );
static bool     jumpShowHandler( const std::list<SuperString>& parameters );
static bool     mediaCreateHandler( const std::list<SuperString>& parameters );
static bool     nodeConfigHandler( const std::list<SuperString>& parameters );
static bool     nodeDeviceHandler( const std::list<SuperString>& parameters );
static bool     nodeIopHandler( const std::list<SuperString>& parameters );
static bool     nodeSubSystemHandler( const std::list<SuperString>& parameters );
static bool     quitHandler( const std::list<SuperString>& parameters );

static bool     getJumpKeys( const std::list<SuperString>&      parameters,
                             std::set<PanelInterface::JUMPKEY>* pJumpKeys );
static void     parameterize( SuperString&                  input,
                              std::list<SuperString>* const pParameters );
static bool     processCommand( const SuperString&  input );
static void     showJumpKeysSet();



//  Command entries

typedef     std::map<std::string, COMMAND_HANDLER>  SUBCOMMANDS;

class  Command
{
public:
    COMMAND_HANDLER         m_Handler;          //  zero for top-level handlers which require a sub-command
    const std::string       m_SyntaxText;
    const std::string       m_HelpText;
    const SUBCOMMANDS*      m_pSubCommands;

    Command( COMMAND_HANDLER            commandHandler,
             const std::string&         syntaxText,
             const std::string&         helpText,
             const SUBCOMMANDS* const   pSubCommands )
            :m_Handler( commandHandler ),
            m_SyntaxText( syntaxText ),
            m_HelpText( helpText ),
            m_pSubCommands( pSubCommands )
    {}

    Command( COMMAND_HANDLER    commandHandler,
             const std::string& syntaxText,
             const std::string& helpText )
            :m_Handler( commandHandler ),
            m_SyntaxText( syntaxText ),
            m_HelpText( helpText ),
            m_pSubCommands( 0 )
    {}
};

typedef std::map<std::string, Command*>     COMMANDS;



static SUBCOMMANDS ExecSubCommands =
{
    {"BOOT",        execBootHandler},
    {"CONFIG",      execConfigHandler},
    {"DUMP",        execDumpHandler},
    {"HALT",        execHaltHandler},
    {"LOAD",        execLoadHandler},
    {"RELOAD",      execReloadHandler},
    {"STATUS",      execStatusHandler},
    {"UNLOAD",      execUnloadHandler},
};

static SUBCOMMANDS JumpSubCommands =
{
    {"CLEAR",       jumpClearHandler},
    {"SET",         jumpSetHandler},
    {"SHOW",        jumpShowHandler},
};

static SUBCOMMANDS MediaSubCommands =
{
    {"CREATE",      mediaCreateHandler},
};

static SUBCOMMANDS NodeSubCommands =
{
    {"CONFIG",      nodeConfigHandler},
    {"DEVICE",      nodeDeviceHandler},
    {"IOP",         nodeIopHandler},
    {"SUBSYSTEM",   nodeSubSystemHandler},
};


static Command CallCommand( callHandler,
                            "/CALL script_name\n",
                            "Executes the emssp commands found in the given script\n",
                            0 );

static Command ExecCommand( 0,
                            "/EXEC [ BOOT | DUMP | HALT | LOAD | RELOAD | STATUS | UNLOAD ]\n"
                                    "/EXEC CONFIG LIST\n"
                                    "/EXEC CONFIG SET {tag} {value}\n"
                                    "/EXEC CONFIG SHOW {tag}\n"
                                    "/EXEC CONFIG [ LOAD | SAVE ] [ file_name ]\n",
                            "The first form invokes the specified sub-command against a loaded (or not) exec image.\n"
                                    "The CONFIG LIST, SET, and SHOW sub-commands manipulate exec configuration values.\n"
                                    "The CONFIG LOAD and SAVE sub-commands load or save the full set of configuration\n"
                                    "values to the specified file, or to the default configuration file.\n",
                            &ExecSubCommands );

static Command HelpCommand( helpHandler,
                            "/HELP [ command ]\n",
                            "Displays a short syntax list of all available commands,\n"
                                "or more detailed help for the indicated command (if specified).\n" );

static Command JumpCommand( 0,
                            "/JUMP SHOW\n"
                                    "/JUMP SET|CLEAR key_number,...\n",
                            "Displays all the jump keys currently set, or sets or clears individual keys.\n"
                                    "   JK3: Prevents the panel from restarting the exec after a stop\n"
                                    "   JK4: Reloads system files from system tape\n"
                                    "   JK5: Force ALLOW CONFIG message at boot time for recovery boots\n"
                                    "   JK7: Initializes TIP\n"
                                    "   JK9: Initializes GENF$ - Print queues and backlog will be lost\n"
                                    "  JK13: Initializes Mass Storage\n",
                            &JumpSubCommands );

static Command MediaCommand( 0,
                             "/MEDIA CREATE PACK pack_name block_size block_count\n"
                                     "/MEDIA CREATE VOLUME volume_name\n",
                             "Creates a disk pack or tape volume to be mounted on an appropriate device.\n"
                                     "pack_name is the name of a FileSystemDisk pack to be created.\n"
                                     "block_size is the size in bytes of an IO to the pack (512 to 8192 inclusive).\n"
                                     "block_count is the number of blocks for the pack.\n"
                                     "    This value must result in a minimum of 10,000 tracks, or 81,920,000 bytes.\n"
                                     "volume_name is the name of a virtual FileSystemTape volume to be created.\n",
                             &MediaSubCommands );

static Command NodeCommand( 0,
                            "/NODE CONFIG [ LOAD | SAVE ] [ {file_name} ]\n"
                                    "/NODE DEVICE CREATE {device_name} {device_type} {device_model} {subsystem_name}\n"
                                    "/NODE DEVICE DELETE {device_name}\n"
                                    "/NODE DEVICE LIST\n"
                                    "/NODE DEVICE MOUNT {device_name} {media_name}\n"
                                    "/NODE DEVICE SHOW {device_name}\n"
                                    "/NODE IOP [ CREATE | LIST ]\n"
                                    "/NODE IOP [ DELETE | SHOW ] {iop_name}\n"
                                    "/NODE SUBSYSTEM CREATE {subsystem_name} [ DISK | TAPE | SYMBIONT ] [ REDUNDANT ]\n"
                                    "/NODE SUBSYSTEM [ DELETE | DETACH | SHOW ] {subsystem_name}\n"
                                    "/NODE SUBSYSTEM LIST\n"
                                    "/NODE SUBSYSTEM ATTACH {subsystem_name} {chmod_name} [ {chmod_name} ]\n",
                            "Manages the hardware inventory for the emulator.\n"
                                    "file_name is the optional name of a hardware configuration file to and from\n"
                                    "  which hardware configuration data is save and loaded.\n"
                                    "  If not specified, the default configuration file is used.\n"
                                    "device_name is 1 to 6 characters including letters and numbers.\n"
                                    "device_type is chosen from [ DISK | SYMBIONT | TAPE ]\n"
                                    "device_model is chosen from\n"
                                    "  [ FILE_SYSTEM_DISK | FILE_SYSTEM_PRINTER | FILE_SYSTEM_READER\n"
                                    "    | FILE_SYSTEM_PUNCH | FILE_SYSTEM_TAPE ]\n"
                                    "subsystem_name is an arbitrarily assigned name for a given subsystem\n"
                                    "media_name is the name of a virtual disk pack or a virtual tape volume\n"
                                    "iop_name is the name of an existing IOProcessor node\n"
                                    "chmod_name is the name of a channel module to which the subsystem is to be attached\n",
                            &NodeSubCommands );

static Command QuitCommand( quitHandler,
                            "/QUIT [ FORCE ]\n",
                            "Ends execution of the emssp executable.\n"
                                    "If an exec is running, you may use the FORCE argument stop it.\n" );


static COMMANDS Commands
{
    { "CALL",   &CallCommand },
    { "EXEC",   &ExecCommand },
    { "HELP",   &HelpCommand },
    { "JUMP",   &JumpCommand },
    { "MEDIA",  &MediaCommand },
    { "NODE",   &NodeCommand },
    { "QUIT",   &QuitCommand },
};


//  Other globals

static SSPConsole               Console( "CONSOL" );
static SSPPanel                 Panel( &Console );
static Configuration*           pConfiguration = 0;
static PersistableNodeTable*    pNodeTable = 0;

static int                      CallDepth = 0;
static const char* const        SyntaxErrorMsg = "Syntax Error";
static bool                     TermFlag = false;



//  ----------------------------------------------------------------------------
//  Command handlers
//  ----------------------------------------------------------------------------

//  callHandler()
//
//  Loads a script and executes the commands there-in
static bool
callhandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        SuperString& fileName = parameters.front();

        std::fstream fileStream;
        fileStream.open( fileName, std::ios_base::in );
        if ( fileStream.fail() )
        {
            std::cout << "Error:Cannot open file '" << fileName << "'" << std::endl;
            return false;
        }

        ++CallDepth;
        std::cout << "[" << CallDepth << "]:Executing '" << fileName << "'..." << std::endl;

        bool failFlag = false;
        while ( !fileStream.eof() )
        {
            std::string inputText;
            std::getline( fileStream, inputText );
            if ( fileStream.fail() )
            {
                failFlag = true;
                break;
            }

            std::cout << "[" << inputText << "]" << std::endl;//????
        }

        if ( failFlag )
            std::cout << "[" << CallDepth << "]:Aborted" << std::endl;
        else
            std::cout << "[" << CallDepth << "]:End of file '" << fileName << "'" << std::endl;

        --CallDepth;
        fileStream.close();

        return !failFlag;
    }

    std::cout << SyntaxErrorMsg << std::endl;
    return false;
}


//  execBootHandler()
//
//  Handles EXEC BOOT
static bool
execBootHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    return Panel.bootExec( true );
}


//  execConfigHandler()
//
//  Handles EXEC CONFIG
static bool
execConfigHandler
(
    const std::list<SuperString>& parameters
)
{
    //???? needs lots of work in execlib::Configuration to support what we want to do here
//  "/EXEC CONFIG LIST\n"
//  "/EXEC CONFIG SET {tag} {value}\n"
//  "/EXEC CONFIG SHOW {tag}\n"
//  "/EXEC CONFIG [ LOAD | SAVE ] [ file_name ]\n",
    std::cout << "Not yet implemented" << std::endl;
    return false;//????
}


//  execDumpHandler()
//
//  Handles EXEC DUMP
static bool
execDumpHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    return Panel.dumpExec();
}


//  execHaltHandler()
//
//  Handles EXEC HALT
static bool
execHaltHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    return Panel.haltExec();
}


//  execLoadHandler()
//
//  Handles EXEC LOAD
static bool
execLoadHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    return Panel.loadExec( pConfiguration, pNodeTable );
}


//  execReloadHandler()
//
//  Handles EXEC RELOAD
static bool
execReloadHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    return Panel.reloadExec( pConfiguration, pNodeTable );
}


//  execStatusHandler()
//
//  Handles EXEC STATUS
static bool
execStatusHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    std::cout << "Status:" << Panel.getStatusMessage() << std::endl;
    std::cout << "Stop:  "
            << std::oct << std::setw( 3 ) << std::setfill( '0' )
            << static_cast<unsigned int>(Panel.getStopCode())
            << ":" << Panel.getStopCodeMessage() << std::endl;
    return true;
}


//  execUnloadHandler()
//
//  Handles EXEC UNLOAD
static bool
execUnloadHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    return Panel.unloadExec();
}


//  helpHandler()
//
//  Handles the HELP command
static bool
helpHandler
(
    const std::list<SuperString>& parameters
)
{
    switch ( parameters.size() )
    {
    case 0:
        for ( auto it : Commands )
            std::cout << it.second->m_SyntaxText;
        return true;

    case 1:
    {
        SuperString cmdRef = parameters.front();
        if ( cmdRef[0] == '/' )
            cmdRef = cmdRef.substr( 1 );
        cmdRef.foldToUpperCase();
        auto it = Commands.find( cmdRef );
        if ( it == Commands.end() )
        {
            std::cout << "Error:Command '/" << cmdRef << "' not recognized" << std::endl;
            return false;
        }

        std::cout << it->second->m_SyntaxText;
        std::cout << std::endl;
        std::cout << it->second->m_HelpText;
        return true;
    }

    default:
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }
}


//  jumpClearHandler()
//
//  Handles JUMP Clear
static bool
jumpClearHandler
(
    const std::list<SuperString>& parameters
)
{
    //  Validate jump key parameters
    std::set<PanelInterface::JUMPKEY> jumpKeys;
    if ( !getJumpKeys( parameters, &jumpKeys ) )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    //  Now update the keys
    for ( auto its = jumpKeys.begin(); its != jumpKeys.end(); ++its )
        Panel.setJumpKey( *its, false );

    //  Show the results
    showJumpKeysSet();
    return true;
}


//  jumpSetHandler()
//
//  Handles JUMP SET
static bool
jumpSetHandler
(
    const std::list<SuperString>& parameters
)
{
    //  Validate jump key parameters
    std::set<PanelInterface::JUMPKEY> jumpKeys;
    if ( !getJumpKeys( parameters, &jumpKeys ) )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    //  Now update the keys
    for ( auto its = jumpKeys.begin(); its != jumpKeys.end(); ++its )
        Panel.setJumpKey( *its, true );

    //  Show the results
    showJumpKeysSet();
    return true;
}


//  jumpShowHandler()
//
//  Handles JUMP SHOW
static bool
jumpShowHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 0 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    showJumpKeysSet();
    return true;
}


//  mediaCreateHandler()
//
//  Handles MEDIA CREATE...
static bool
mediaCreateHandler
(
    const std::list<SuperString>& parameters
)
{
    auto it = parameters.begin();

    if ( it != parameters.end() )
    {
        if ( (*it).compareNoCase( "PACK" ) == 0 )
        {
            ++it;
            SuperString packName;
            BLOCK_SIZE blockSize = 0;
            BLOCK_COUNT blockCount = 0;

            if ( it != parameters.end() )
            {
                packName = *it;
                packName.foldToUpperCase();
                ++it;
            }

            if ( it != parameters.end() )
            {
                SuperString blockSizeStr = *it;
                ++it;
                if ( blockSizeStr.isDecimalNumeric() )
                    blockSize = blockSizeStr.toDecimal();
            }

            if ( it != parameters.end() )
            {
                SuperString blockCountStr = *it;
                ++it;
                if ( blockCountStr.isDecimalNumeric() )
                    blockCount = blockCountStr.toDecimal();
            }

            if ( packName.empty() || ( blockCount == 0 ) || ( blockSize == 0 ) )
            {
                std::cout << SyntaxErrorMsg << std::endl;
                return false;
            }

            if ( !miscIsValidPackName( packName ) )
            {
                std::cout << "Error:Invalid pack name" << std::endl;
                return false;
            }

            std::string fileName = PACKS_PATH + packName + ".dsk";
            std::string errorMsg;
            if ( !FileSystemDiskDevice::createPack( fileName, blockSize, blockCount, &errorMsg ) )
            {
                std::cout << "Error:" << errorMsg << std::endl;
                return false;
            }

            std::cout << "Pack " << packName << " created in file " << fileName << std::endl;
            return true;
        }
        else if ( (*it).compareNoCase( "VOLUME" ) == 0 )
        {
            //TODO:TAPE
            std::cout << "Not yet implemented" << std::endl;
            return false;
        }
    }

    std::cout << SyntaxErrorMsg << std::endl;
    return false;
}


//  nodeConfigHandler()
//
//  Handles NODE CONFIG...
static bool
nodeConfigHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( (parameters.size() > 0) && (parameters.size() <= 2) )
    {
        SuperString fileName;
        if ( parameters.size() == 1 )
            fileName = NODE_CONFIG_FILE_NAME;
        else
            fileName = parameters.back();

        if ( parameters.front().compareNoCase( "LOAD" ) == 0 )
        {
            PersistableNodeTable::Result result = pNodeTable->load( fileName );
            if ( result != PersistableNodeTable::Result::Success )
            {
                std::cout << "Error:" << PersistableNodeTable::getResultString( result );
                return false;
            }

            std::cout << "Hardware configuration loaded" << std::endl;
            return true;
        }
        else if ( parameters.front().compareNoCase( "SAVE" ) == 0 )
        {
            PersistableNodeTable::Result result = pNodeTable->save( fileName );
            if ( result != PersistableNodeTable::Result::Success )
            {
                std::cout << "Error:" << PersistableNodeTable::getResultString( result );
                return false;
            }

            std::cout << "Hardware configuration saved" << std::endl;
            return true;
        }
    }

    std::cout << SyntaxErrorMsg << std::endl;
    return false;
}


//  nodeConfigHandler()
//
//  Handles NODE DEVICE...
static bool
nodeDeviceHandler
(
    const std::list<SuperString>& parameters
)
{
//  "/NODE DEVICE CREATE {device_name} {device_type} {device_model} {subsystem_name}\n"
//  "/NODE DEVICE DELETE {device_name}\n"
//  "/NODE DEVICE LIST\n"
//  "/NODE DEVICE MOUNT {device_name} {media_name}\n"
//  "/NODE DEVICE SHOW {device_name}\n"
    std::cout << "Not yet implemented" << std::endl;
    return false;//????
}


//  nodeConfigHandler()
//
//  Handles NODE IOP...
static bool
nodeIopHandler
(
    const std::list<SuperString>& parameters
)
{
//  "/NODE IOP [ CREATE | LIST ]\n"
//  "/NODE IOP [ DELETE | SHOW ] {iop_name}\n"
    if ( parameters.size() > 0 )
    {
        if ( parameters.front().compareNoCase( "CREATE" ) == 0 )
        {
            PersistableNodeTable::Result result = pNodeTable->createIoProcessor();
            if ( result != PersistableNodeTable::Result::Success )
            {
                std::cout << "Error:" << PersistableNodeTable::getResultString( result );
                return false;
            }

            std::cout << "IOProcessor created" << std::endl;
            return true;
        }
        else if ( parameters.front().compareNoCase( "DELETE" ) == 0 )
        {
            return true;//????
        }
        else if ( parameters.front().compareNoCase( "LIST" ) == 0 )
        {
            const PersistableNodeTable::IOPROCESSORS& iops = pNodeTable->getIoProcessors();
            for ( auto it = iops.begin(); it != iops.end(); ++it )
            {
                const IOProcessor* piop = it->second;
                std::cout << "IOProcessor " << piop->getName() << " contains the following channel modules:" << std::endl;
                const Node::CHILDNODES& chmods = piop->getChildNodes();
                for ( auto itcm = chmods.begin(); itcm != chmods.end(); ++itcm )
                    std::cout << "  " << itcm->first << ":  " << itcm->second->getName() << std::endl;
            }
            return true;
        }
        else if ( parameters.front().compareNoCase( "SHOW" ) == 0 )
        {
            return true;//????
        }
    }

    std::cout << SyntaxErrorMsg << std::endl;
    return false;
}


//  nodeConfigHandler()
//
//  Handles NODE SUBSYSTEM...
static bool
nodeSubSystemHandler
(
    const std::list<SuperString>& parameters
)
{
//  "/NODE SUBSYSTEM CREATE {subsystem_name} [ DISK | TAPE | SYMBIONT ] [ REDUNDANT ]\n"
//  "/NODE SUBSYSTEM [ DELETE | DETACH | SHOW ] {subsystem_name}\n"
//  "/NODE SUBSYSTEM LIST\n"
//  "/NODE SUBSYSTEM ATTACH {subsystem_name} {chmod_name} [ {chmod_name} ]\n",
    std::cout << "Not yet implemented" << std::endl;
    return false;//????
}


#if 0 //OLD SHIT
//  nodeCreateHandler()
//
//  Handles NODE CREATE...
static bool
nodeCreateHandler
(
    const std::list<SuperString>& parameters
)
{
    //????
    /*      "/NODE CREATE node_name PROCESSOR IOPROCESSOR upi_number\n"
            "/NODE CREATE node_name CHANNEL_MODULE\n"
            "/NODE CREATE node_name CONTROLLER [ DISK | SYMBIONT | TAPE ]\n"
            "/NODE CREATE node_name DEVICE [ DISK | TAPE ] FILESYSTEM\n"
            "/NODE CREATE node_name DEVICE [ PRINTER | PUNCH | READER ] FILESYSTEM directory\n"
            "/NODE CREATE node_name DEVICE TERMINAL terminal_type\n",
    */
    std::cout << "Not yet implemented" << std::endl;
    return false;
}


//  nodeDeleteHandler()
//
//  Handles NODE DELETE...
static bool
nodeDeleteHandler
(
    const std::list<SuperString>& parameters
)
{
    /*      "/NODE DELETE node_name\n"     */
    //????
    std::cout << "Not yet implemented" << std::endl;
    return false;
}


//  nodeListHandler()
//
//  Handles NODE LIST...
static bool
nodeListHandler
(
    const std::list<SuperString>& parameters
)
{
    std::set<Node*> nodes;
    for ( auto it = parameters.begin(); it != parameters.end(); ++it )
    {
        Node* pNode = pNodeTable->getNode( *it );
        if ( pNode == 0 )
        {
            std::cout << "No node exists with name " << *it << std::endl;
            return false;
        }

        nodes.insert( pNode );
    }

    if ( nodes.size() == 0 )
    {
        const std::set<Node*>& allNodes = pNodeTable->getNodeSet();
        for ( auto it = allNodes.begin(); it != allNodes.end(); ++it )
            nodes.insert( *it );
    }

    for ( auto it = nodes.begin(); it != nodes.end(); ++it )
    {
        Node* pNode = *it;
        std::cout << "Node " << pNode->getName() << " " << pNode->getCategoryString();
        switch ( pNode->getCategory() )
        {
        case Node::Category::CHANNEL_MODULE:
            break;

        case Node::Category::CONTROLLER:
        {
            Controller* pController = dynamic_cast<Controller*>( pNode );
            std::cout << " " << pController->getControllerTypeString();
            break;
        }

        case Node::Category::DEVICE:
        {
            Device* pDevice = dynamic_cast<Device*>( pNode );
            std::cout << " " << pDevice->getDeviceTypeString();
            std::cout << " " << pDevice->getDeviceModelString();
            if ( pDevice->isReady() )
                std::cout << " [READY]";
            switch ( pDevice->getDeviceType() )
            {
            case Device::DeviceType::DISK:
            {
                DiskDevice* pdd = dynamic_cast<DiskDevice*>( pDevice );
                if ( pdd->isMounted() )
                    std::cout << " [MNTD] BlockSize=" << pdd->getBlockSize() << " Blocks=" << pdd->getBlockCount();
                break;
            }

            case Device::DeviceType::SYMBIONT:
                break;

            case Device::DeviceType::TAPE:
                //TODO:TAPE
                break;
            }
            break;
        }

        case Node::Category::PROCESSOR:
            break;
        }
        std::cout << std::endl;
    }

    return true;
}


//  nodeLoadHandler()
//
//  Handles NODE LOAD...
static bool
nodeLoadHandler
(
    const std::list<SuperString>& parameters
)
{
    std::cout << "Error:Not yet implemented" << std::endl;
    return false;
}


//  nodeMountHandler()
//
//  Handles NODE MOUNT...
static bool
nodeMountHandler
(
    const std::list<SuperString>& parameters
)
{
    auto it = parameters.begin();

    if ( it != parameters.end() )
    {
        if ( (*it).compareNoCase( "PACK" ) == 0 )
        {
            ++it;
            SuperString mediaName;
            SuperString nodeName;
            bool readOnly = false;

            if ( it != parameters.end() )
            {
                mediaName = *it;
                mediaName.foldToUpperCase();
                ++it;
            }

            if ( it != parameters.end() )
            {
                nodeName = *it;
                nodeName.foldToUpperCase();
                ++it;
            }

            if ( it != parameters.end() )
            {
                SuperString token = *it;
                ++it;
                token.foldToUpperCase();
                if ( token.compare( "READ_ONLY" ) != 0 )
                {
                    std::cout << SyntaxErrorMsg << std::endl;
                    return false;
                }

                readOnly = true;
            }

            if ( mediaName.empty() )
            {
                std::cout << SyntaxErrorMsg << std::endl;
                return false;
            }

            FileSystemDiskDevice* pDisk = dynamic_cast<FileSystemDiskDevice*>( pNodeTable->getNode( nodeName ) );
            if ( pDisk == 0 )
            {
                std::cout << "Error:" << nodeName << " is not a FileSystemDiskDevice" << std::endl;
                return false;
            }

            if ( pDisk->isMounted() )
            {
                std::cout << "Error:" << nodeName << " already has a pack mounted" << std::endl;
                return false;
            }

            std::string fileName = PACKS_PATH + mediaName + ".dsk";
            if ( !pDisk->mount( fileName ) )
            {
                std::cout << "Error:Cannot mount " << mediaName << " on " << nodeName << std::endl;
                return false;
            }

            std::cout << mediaName << " (file " << fileName << ") mounted on " << nodeName << std::endl;
            pDisk->setIsWriteProtected( readOnly );
            pDisk->setReady( true );
            return true;
        }
        else if ( (*it).compareNoCase( "VOLUME" ) == 0 )
        {
            //TODO:TAPE
            std::cout << "Not yet implemented" << std::endl;
            return false;
        }
    }

    std::cout << SyntaxErrorMsg << std::endl;
    return false;
}


//  nodeSaveHandler()
//
//  Handles NODE SAVE...
static bool
nodeSaveHandler
(
    const std::list<SuperString>& parameters
)
{
    if ( parameters.size() > 1 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    std::string fileName;
    if ( parameters.size() == 1 )
        fileName = parameters.front();
    else
        fileName.append( NODE_CONFIG_FILE_NAME );

    if ( !pNodeTable->save( fileName ) )
    {
        std::cout << "Error saving node config:"
                << pNodeTable->getLastErrorMessage()
                << std::endl;
        return false;
    }

    std::cout << "Node configuration saved" << std::endl;
    return true;
}


//  nodeUnmountHandler()
//
//  Handles NODE UNMOUNT...
static bool
nodeUnmountHandler
(
    const std::list<SuperString>& parameters
)
{
    /*  MEDIA UNMOUNT [ pack_name | device_name ]   */
    auto it = parameters.begin();

    if ( it != parameters.end() )
    {
        SuperString nodeName = *it;
        FileSystemDiskDevice* pDisk = dynamic_cast<FileSystemDiskDevice*>( pNodeTable->getNode( nodeName ) );
        if ( pDisk )
        {
            if ( !pDisk->isMounted() )
            {
                std::cout << "Warn:" << nodeName << " is not mounted" << std::endl;
                return true;
            }
            else
            {
                if ( !pDisk->unmount() )
                {
                    std::cout << "Error:Failed to unmount " << nodeName << std::endl;
                    return false;
                }

                std::cout << "Device " << nodeName << " unmounted" << std::endl;
                return true;
            }
        }

        //TODO:TAPE

        std::cout << "Error:" << nodeName << " is not a FileSystemDisk or a FileSystemTape" << std::endl;
        return false;
    }

    std::cout << SyntaxErrorMsg << std::endl;
    return false;
}
#endif


//  quitHandler()
//
//  Handles the QUIT command
static bool
quitHandler
(
    const std::list<SuperString>& parameters
)
{
    bool forceFlag = false;

    if ( parameters.size() > 1 )
    {
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }
    else if ( parameters.size() == 1 )
    {
        if ( parameters.front().compareNoCase( "FORCE" ) == 0 )
            forceFlag = true;
        else
        {
            std::cout << SyntaxErrorMsg << std::endl;
            return false;
        }
    }

    //  Is an EXEC running?
    if ( Panel.isExecLoaded() && Panel.isExecRunning() )
    {
        if ( !forceFlag )
        {
            std::cout << "Error:Cannot quit with exec running." << std::endl;
            return false;
        }

        std::cout << "Halting exec..." << std::endl;
        Panel.haltExec();
    }

    TermFlag = true;
    std::cout << "Terminating..." << std::endl;
    return true;
}


//  ----------------------------------------------------------------------------
//  Useful static functions
//  ----------------------------------------------------------------------------

//  getJumpKeys()
//
//  Reads the argument list, and populates a given set of jump key values.
//  Returns false for syntax error
static bool
getJumpKeys
(
    const std::list<SuperString>&       parameters,
    std::set<PanelInterface::JUMPKEY>*  pJumpKeys
)
{
    if ( parameters.size() == 0 )
        return false;

    for ( auto it = parameters.begin(); it != parameters.end(); ++it )
    {
        if ( !it->isDecimalNumeric() )
            return false;

        PanelInterface::JUMPKEY jk = it->toDecimal();
        if ( (jk < 1) || (jk > 36) )
            return false;

        pJumpKeys->insert( jk );
    }

    return true;
}


//  parameterize()
//
//  Split a space-delimited string of tokens into a list of the tokens
//
//  Parameters:
//      input:          string containing parameters
//      pParameters:    pointer to container which we populate
static void
parameterize
(
    SuperString&                    input,
    std::list<SuperString>* const   pParameters
)
{
    pParameters->clear();

    input.trimLeadingSpaces();
    SuperString token = input.strip( ' ' );
    while ( !token.empty() )
    {
        pParameters->push_back( token );
        input.trimLeadingSpaces();
        token = input.strip( ' ' );
    }
}


//  processCommand()
//
//  Processes a line of input representing a command.
//  Returns the result (an empty command is silently passed as true).
//
//  We just expect the first character to be a slash, and eat it.
static bool
processCommand
(
    const SuperString&  input
)
{
    SuperString temp = input.substr( 1 );
    temp.trimLeadingSpaces();
    if ( temp.size() == 0 )
        return true;

    temp.trimLeadingSpaces();
    SuperString command = temp.strip( ' ' );
    command.foldToUpperCase();

    auto it = Commands.find( command );
    if ( it == Commands.end() )
    {
        std::cout << "Error:Command '/" << command << "' not recognized" << std::endl;
        return false;
    }

    COMMAND_HANDLER handler = it->second->m_Handler;
    std::list<SuperString> parameters;

    //  Does the command take a sub-command parameter?
    if ( it->second->m_pSubCommands == 0 )
    {
        //  No - Is there a handler?
        if ( handler == 0 )
        {
            //  Oops - we have nothing to call.
            std::cout << "Internal Error" << std::endl;
            return false;
        }

        //  Parameterize the rest of temp and call the handler
        parameterize( temp, &parameters );
        return handler( parameters );
    }

    //  Command takes a sub-command - do we have one?
    //  (this would be the first token in the remainder of temp)
    temp.trimLeadingSpaces();
    SuperString subCommand = temp.strip( ' ' );
    if ( subCommand.empty() )
    {
        //  No sub-command given - if there isn't a command handler for the main command, oops.
        if ( handler == 0 )
        {
            std::cout << SyntaxErrorMsg << std::endl;
            return false;
        }

        //  We have a handler call it with the empty parameter list.
        return handler( parameters );
    }

    //  We have a sub-command.  Is it valid?
    subCommand.foldToUpperCase();
    auto itsub = it->second->m_pSubCommands->find( subCommand );
    if ( itsub == it->second->m_pSubCommands->end() )
    {
        //  Oops - subcommand not recognized.
        std::cout << SyntaxErrorMsg << std::endl;
        return false;
    }

    //  It is valid - parameterize the rest of temp, and call the subcommand handler.
    handler = itsub->second;
    if ( handler == 0 )
    {
        //  Oops - we have nothing to call.
        std::cout << "Internal Error" << std::endl;
        return false;
    }

    parameterize( temp, &parameters );
    return handler( parameters );
}


//  showJumpKeysSet()
//
//  Displays a list of all jump keys which are currently set
static void
showJumpKeysSet()
{
    std::cout << "Jump Keys Set:" << std::endl;
    bool set = false;
    for ( PanelInterface::JUMPKEY jk = 1; jk <= 36; ++jk )
    {
        if ( Panel.getJumpKey( jk ) )
        {
            std::cout << " " << std::setw( 2 ) << std::setfill( '0' ) << jk;
            set = true;
        }
    }

    if ( !set )
        std::cout << " <NONE>";
    std::cout << std::endl;
}


//  ----------------------------------------------------------------------------
//  Main
//  ----------------------------------------------------------------------------

int
main
(
    int         argc,
    char**      argv
)
{
    std::cout << "emssp " << VERSION << std::endl;
    std::cout << COPYRIGHT << std::endl;

    //???? need a SystemLog better than this
    SystemLog* pLog = new SystemLog();
    pLog->open( "emssp.log" );

    //  Initialization
    pNodeTable = new PersistableNodeTable();
#if 0 // obsolete
    {
        IOProcessor* piop0 = new IOProcessor( "IOP0" );
        ChannelModule* pchmod00 = new ChannelModule( "CHM0-0" );
        pchmod00->startUp();
        Controller* pdcua = new DiskController( "DCUA" );
        Controller* pdcub = new DiskController( "DCUB" );
        FileSystemDiskDevice* pdisk0 = new FileSystemDiskDevice( "DISK0" );
        FileSystemDiskDevice* pdisk1 = new FileSystemDiskDevice( "DISK1" );
        FileSystemDiskDevice* pdisk2 = new FileSystemDiskDevice( "DISK2" );
        FileSystemDiskDevice* pdisk3 = new FileSystemDiskDevice( "DISK3" );

        piop0->registerChildNode( 0, pchmod00 );
        pchmod00->registerChildNode( 0, pdcua );
        pchmod00->registerChildNode( 1, pdcub );
        pdcua->registerChildNode( 0, pdisk0 );
        pdcua->registerChildNode( 1, pdisk1 );
        pdcua->registerChildNode( 2, pdisk2 );
        pdcua->registerChildNode( 3, pdisk3 );
        pdcub->registerChildNode( 0, pdisk0 );
        pdcub->registerChildNode( 1, pdisk1 );
        pdcub->registerChildNode( 2, pdisk2 );
        pdcub->registerChildNode( 3, pdisk3 );

        pdisk0->mount( "/opt/em2200/packs/FIX000.dsk" );
        pdisk0->setReady( true );
        pdisk0->setIsWriteProtected( false );

        pdisk1->mount( "/opt/em2200/packs/FIX001.dsk" );
        pdisk1->setReady( true );
        pdisk1->setIsWriteProtected( false );

        pdisk2->mount( "/opt/em2200/packs/FIX002.dsk" );
        pdisk2->setReady( true );
        pdisk2->setIsWriteProtected( false );

        pdisk3->mount( "/opt/em2200/packs/FIX003.dsk" );
        pdisk3->setReady( true );
        pdisk3->setIsWriteProtected( false );

        pNodeTable->addNode( piop0 );
        pNodeTable->addNode( pchmod00 );
        pNodeTable->addNode( pdcua );
        pNodeTable->addNode( pdcub );
        pNodeTable->addNode( pdisk0 );
        pNodeTable->addNode( pdisk1 );
        pNodeTable->addNode( pdisk2 );
        pNodeTable->addNode( pdisk3 );
    }
#endif

    pConfiguration = new Configuration();

    //  Command loop
    char inputBuffer[132];
    while ( !TermFlag )
    {
        //  Get input from the user
        std::cout << std::endl << ">";
        std::cin.getline( inputBuffer, 132 );
        SuperString rawInput( inputBuffer );
        rawInput.trimLeadingSpaces();
        rawInput.trimTrailingSpaces();
        if ( rawInput.size() > 0 )
        {
            //  Does the input begin with a slash character?  If so, it's a command
            if ( rawInput[0] == '/' )
            {
                if ( processCommand( rawInput ) )
                    continue;
            }
            else
            {
                //  Not a command, it must be an attempt to communicate with the console
                if ( !Panel.isExecLoaded() || !Panel.isExecRunning() )
                    std::cout << "EXEC is not loaded or not running - console input ignored." << std::endl;
                else
                    Console.injectConsoleMessage( rawInput );
                continue;
            }
        }

        std::cout << "Type /HELP for assistance" << std::endl;
    }

    //  Termination
    //???? lose all Node entries...

    pLog->close();
    delete pLog;
    pLog = 0;

    return 0;
}