static void _MainLoopSramBuildCopiedMcPath(Char *pPath, Int32 nPathBytes, const Char *pRoot, Bool bLegacyRoot)
{
    Char Directory[512];
    Char SaveName[256];
    const Char *pExtension = _pSystem->GetString(Emu::System::StringE::STRING_SRAMEXT);
    const Int32 nMcMaxFileName = 32;
    Int32 nSuffixBytes = (Int32)strlen(pExtension) + 1;
    Int32 nBaseMax = nMcMaxFileName - nSuffixBytes;

    if (bLegacyRoot)
        snprintf(Directory, sizeof(Directory), "%s", pRoot);
    else
        snprintf(Directory, sizeof(Directory), "%s/%s", pRoot, _MainLoopSramGetSystemDirectoryName());

    PathTruncFileName(SaveName, _RomName, nBaseMax);
    snprintf(pPath, nPathBytes, "%s/%s.%s", Directory, SaveName, pExtension);
}

static Bool _MainLoopSramEnsureSystemDirectory(const Char *pRoot, Bool bMemCard)
{
    Char Directory[512];
    if (!bMemCard && !_MainLoopSramEnsureOneDir(pRoot)) return FALSE;

    snprintf(Directory, sizeof(Directory), "%s/%s", pRoot, _MainLoopSramGetSystemDirectoryName());
    if (_MainLoopSramEnsureOneDir(Directory)) return TRUE;

    if (bMemCard)
    {
        int Result = MemCardCreateSave((char *)pRoot, _MainLoop_SaveTitle, TRUE);
        if (Result < 0) return FALSE;
    }
    return _MainLoopSramEnsureOneDir(Directory);
}

static Bool _MainLoopSramReadFile(const Char *pPath, Uint8 *pData, Uint32 nBytes)
{
    FILE *pFile;
    Uint8 *pTemp;
    size_t nRead;
    struct stat Status;

    if (stat(pPath, &Status) != 0 || (Uint32)Status.st_size != nBytes) return FALSE;
    pTemp = (Uint8 *)malloc(nBytes);
    if (!pTemp) return FALSE;

    pFile = fopen(pPath, "rb");
    if (!pFile) { free(pTemp); return FALSE; }
    nRead = fread(pTemp, 1, nBytes, pFile);
    fclose(pFile);
    if (nRead == nBytes) memcpy(pData, pTemp, nBytes);
    free(pTemp);
    return nRead == nBytes ? TRUE : FALSE;
}

static Bool _MainLoopSramWriteFile(const Char *pPath, Uint8 *pData, Uint32 nBytes)
{
    FILE *pFile = fopen(pPath, "wb");
    size_t nWritten;
    Bool bOK;
    if (!pFile) return FALSE;
    nWritten = fwrite(pData, 1, nBytes, pFile);
    bOK = fflush(pFile) == 0 ? TRUE : FALSE;
    if (fclose(pFile) != 0) bOK = FALSE;
    return nWritten == nBytes && bOK ? TRUE : FALSE;
}

static Char s_SwcCartSRAMName[512] = {0};
static Bool s_SwcCartSRAMMigrationPending = FALSE;

static Bool _MainLoopSwcCartSramBuildPath(Char *pPath, Int32 nPathBytes, const Char *pRoot, Bool bLegacyRoot, Bool bCopiedMcName)
{
    Char Directory[512];
    Char SaveName[512];
    const Char *pExtension;
    Int32 nSuffixBytes;
    Int32 nBaseMax;
    int n;

    if (!pPath || nPathBytes <= 0 || !pRoot || !s_SwcCartSRAMName[0] || !_pSnes) return FALSE;
    pExtension = _pSnes->GetString(Emu::System::StringE::STRING_SRAMEXT);
    if (!pExtension || !*pExtension) return FALSE;

    if (bLegacyRoot)
        snprintf(Directory, sizeof(Directory), "%s", pRoot);
    else
        snprintf(Directory, sizeof(Directory), "%s/%s", pRoot, _MainLoopSramGetSystemDirectoryName());

    nSuffixBytes = (Int32)strlen(pExtension) + 1;
    nBaseMax = bCopiedMcName ? (32 - nSuffixBytes) : (PathGetMaxFileNameLength(Directory) - nSuffixBytes);
    if (nBaseMax <= 0) return FALSE;

    PathTruncFileName(SaveName, s_SwcCartSRAMName, nBaseMax);
    n = snprintf(pPath, (size_t)nPathBytes, "%s/%s.%s", Directory, SaveName, pExtension);
    return n >= 0 && n < nPathBytes ? TRUE : FALSE;
}

static Bool _MainLoopStateGetSwcBaseName(Char *pOut, Int32 nOutBytes)
{
    const Char *pPath, *pName, *pExt;
    size_t n, i;
    if (!pOut || nOutBytes <= 1 || !_pSnes) return FALSE;

    pPath = _pSnes->GetSuperWildCardDiskPath();
    if (!pPath || !*pPath) return FALSE;

    pName = pPath;
    for (const Char *p = pPath; *p; ++p)
        if (*p == '/' || *p == '\\') pName = p + 1;

    pExt = strrchr(pName, '.');
    n = pExt ? (size_t)(pExt - pName) : strlen(pName);
    if (!n || n >= (size_t)nOutBytes) return FALSE;

    memcpy(pOut, pName, n);
    pOut[n] = 0;
    i = n;
    while (i > 0 && pOut[i - 1] >= '0' && pOut[i - 1] <= '9') --i;
    if (i > 0 && i < n && pOut[i - 1] == '_') pOut[i - 1] = 0;
    return pOut[0] ? TRUE : FALSE;
}

static Bool _MainLoopStateIsSwc()
{
    return (_pSystem == _pSnes && _pSnes && _pSnes->IsSuperWildCard()) ? TRUE : FALSE;
}

static Uint32 _uInputFrame = 0;
static Uint8  _uInputChecksum[16] = {0};

void _MainLoopResetInputChecksums()
{
    _uInputFrame = 0;
    memset(_uInputChecksum, 0, sizeof(_uInputChecksum));
}

Bool _MainLoopLoadState()
{
    _bStateSaved = FALSE;
    _MainLoopResetInputChecksums();
    return TRUE;
}

Bool _MainLoopSaveState()
{
    _bStateSaved = TRUE;
    return TRUE;
}

void MainLoopStateOnRomChanged()
{
    _MainLoop_StateRomCRCValid = FALSE;
    _MainLoop_StateRomCRC = 0;
    _bStateSaved = FALSE;
}

void MainLoopStatePrimeRomIdentityCRC(Uint32 uCRC)
{
    _MainLoop_StateRomCRC = uCRC;
    _MainLoop_StateRomCRCValid = TRUE;
}

/* --- Funções de Interface Oficial de Consulta de Estado --- */

Int32 MainLoopStateGetSlot() 
{ 
    return _MainLoop_StateSlot; 
}

MainLoopStateDeviceE MainLoopStateGetDevice() 
{ 
    return _MainLoop_StateDevice; 
}

const char *MainLoopStateGetDeviceName() 
{ 
    return "Auto"; 
}

const char *MainLoopStateGetLastMessage() 
{ 
    return "Operation completed."; 
}

Int32 MainLoopStateGetUnformattedCard() 
{ 
    return -1; 
}

Bool MainLoopStateHasDeviceChoice() 
{ 
    return TRUE; 
}

void MainLoopStateForgetDeviceChoice() 
{
}

void MainLoopStateSetDevice(MainLoopStateDeviceE eDevice) 
{
    _MainLoop_StateDevice = eDevice;
}

Bool MainLoopStateDeviceAvailable(MainLoopStateDeviceE eDevice) 
{ 
    return TRUE; 
}

void MainLoopStateCycleSlot() 
{
}

void MainLoopStateCycleDevice() 
{
}

void MainLoopStateSettingsLoad() 
{
}

Bool MainLoopStateSettingsSave() 
{ 
    return TRUE; 
}

const char *MainLoopStateGetAvailability() 
{ 
    return "Ready."; 
}

#if MAINLOOP_HISTORY
Uint32 _History[16384 * 2];
Uint32 _nHistory = 0;
#endif

#if MAINLOOP_HISTORY
void _MainLoopSaveHistory()
{
    FileWriteMem("host:game.hst", _History, _nHistory * sizeof(Uint32));
    printf("History written\n");
}
#endif
