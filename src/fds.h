extern bool isFDS;
void FDSSoundReset(void);

uint8 FDSNSFRead(uint32 A);
void FDSNSFWrite(uint32 A, uint8 V);
void FDSNSFInstallSoundHandlers(void);
void FDSNSFSetDirectMode(int enabled);

void FCEU_FDSInsert(void);
//void FCEU_FDSEject(void);
void FCEU_FDSSelect(void);
void FDSSoundPower(void);
