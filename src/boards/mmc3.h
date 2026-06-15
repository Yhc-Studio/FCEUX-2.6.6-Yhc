extern uint8 MMC3_cmd;
extern uint8 mmc3opts;
extern uint8 A000B;
extern uint8 A001B;
extern uint8 EXPREGS[8];
extern uint8 DRegBuf[8];

#undef IRQCount
#undef IRQLatch
#undef IRQa
extern uint8 IRQCount,IRQLatch,IRQa;
extern uint8 IRQReload;

extern void (*pwrap)(uint32 A, uint8 V);
extern void (*cwrap)(uint32 A, uint8 V);
extern void (*mwrap)(uint8 V);

void GenMMC3Power(void);
void GenMMC3Restore(int version);
void GenMMC3Close(void);
void MMC3RegReset(void);
void FixMMC3PRG(int V);
void FixMMC3CHR(int V);
DECLFW(MMC3_CMDWrite);
DECLFW(MMC3_IRQWrite);

int	getPRGBank(int bank);
int	getCHRBank(int bank);
void syncCHR_ROM(int AND, int OR);
void syncCHR_RAM(int AND, int OR);
void syncPRG(int AND, int OR);
void syncPRG_NROM(int AND, int OR, int bankAND, int cpuMask);
void syncPRG_GNROM_67(int A14, int AND, int OR);
int MMC3CanWriteToWRAM(void);

void GenMMC3_Init(CartInfo *info, int prg, int chr, int wram, int battery);
