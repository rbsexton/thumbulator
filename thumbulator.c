
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "thumbulator_svc.h"

uint32_t read32 ( uint32_t );

uint32_t read_register ( uint32_t );

#define DBUGFETCH   0
#define DBUGRAM     0
#define DBUGRAMW    0
#define DBUGREG     0
#define DBUG        0

// -----------------------------------------------------------------
// Argument processing variables
// -----------------------------------------------------------------
int diss = 1; // Default to one.
int display_counters = 1; 

#define ROMADDMASK 0xFFFFF
#define RAMADDMASK 0xFFFFF

#define ROMSIZE (ROMADDMASK+1)
#define RAMSIZE (RAMADDMASK+1)

unsigned short rom[ROMSIZE>>1];
unsigned short ram[RAMSIZE>>1];

#define CPSR_N (1<<31)
#define CPSR_Z (1<<30)
#define CPSR_C (1<<29)
#define CPSR_V (1<<28)
#define CPSR_Q (1<<27)

#define CPSR_APSR (CPSR_N|CPSR_Z|CPSR_C|CPSR_V)


unsigned int vcdcount;
unsigned int output_vcd;
FILE *fpvcd;

// Systick Registers
uint32_t systick_ctrl;
uint32_t systick_reload;
uint32_t systick_count;
uint32_t systick_calibrate;

uint32_t halfadd;
uint32_t cpsr;
unsigned int handler_mode;
uint32_t reg_norm[16]; //normal execution mode, do not have a thread mode

unsigned long instructions;
unsigned long fetches;
unsigned long reads;
unsigned long writes;
unsigned long systick_ints;

typedef enum
{ 
  reg_r0 = 0,
  reg_r1 = 1,
  reg_r2 = 2,
  reg_r3 = 3,
  reg_ip = 12, // Use gcc nomenclature
  reg_sp = 13, 
  reg_lr = 14,
  reg_pc = 15, 
} e_regs; 

//-------------------------------------------------------------------
// Support Functions 
//-------------------------------------------------------------------
void dump_counters ( void )
{
    fprintf(stderr,"\n\n");
    fprintf(stderr,"instructions %lu\n",instructions);
    fprintf(stderr,"fetches      %lu\n",fetches);
    fprintf(stderr,"reads        %lu\n",reads);
    fprintf(stderr,"writes       %lu\n",writes);
    fprintf(stderr,"memcycles    %lu\n",fetches+reads+writes);
    fprintf(stderr,"systick_ints %lu\n",systick_ints);
}

//-------------------------------------------------------------------
//-------------------------------------------------------------------
// Memory Operations  
//-------------------------------------------------------------------
//-------------------------------------------------------------------

//-------------------------------------------------------------------
uint32_t fetch16 ( uint32_t addr )
{
    uint32_t data;

    fetches++;

if(DBUGFETCH) fprintf(stderr,"fetch16(0x%08X)=",addr);
if(DBUG) fprintf(stderr,"fetch16(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case 0x00000000: //ROM
            addr&=ROMADDMASK;

//if(addr<0x50)
//{
//    fprintf(stderr,"fetch16(0x%08X), abort\n",addr);
//    exit(1);
//}

            addr>>=1;
            data=rom[addr];
if(DBUGFETCH) fprintf(stderr,"0x%04X\n",data);
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
        case SRAMBASE: //RAM
            addr&=RAMADDMASK;
            addr>>=1;
            data=ram[addr];
if(DBUGFETCH) fprintf(stderr,"0x%04X\n",data);
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
    }
    fprintf(stderr,"fetch16(0x%08X), abort pc = 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
uint32_t fetch32 ( uint32_t addr )
{
    uint32_t data;

if(DBUGFETCH) fprintf(stderr,"fetch32(0x%08X)=",addr);
if(DBUG) fprintf(stderr,"fetch32(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case 0x00000000: //ROM
            // Check for low addresses and apply special rules.
            if(addr<0x50)
            {
                data=read32(addr);
if(DBUGFETCH) fprintf(stderr,"0x%08X\n",data);
if(DBUG) fprintf(stderr,"0x%08X\n",data);
                if(addr==0x00000000) return(data);
                if(addr==0x00000004) return(data);
                if(addr==0x0000003C) return(data);
                fprintf(stderr,"fetch32(0x%08X), abort pc = 0x%04X\n",addr,read_register(15));
                exit(1);
            }
        case SRAMBASE: //RAM
            //data=fetch16(addr+0);
            //data|=((unsigned int)fetch16(addr+2))<<16;
            data=read32(addr);
if(DBUGFETCH) fprintf(stderr,"0x%08X\n",data);
if(DBUG) fprintf(stderr,"0x%08X\n",data);
            return(data);
    }
    fprintf(stderr,"fetch32(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
void write16 ( uint32_t addr, uint32_t data )
{

    writes++;


if(DBUG) fprintf(stderr,"write16(0x%08X,0x%04X)\n",addr,data);
    switch(addr&0xF0000000)
    {
        case SRAMBASE: //RAM
if(DBUGRAM) fprintf(stderr,"write16(0x%08X,0x%04X)\n",addr,data);
            addr&=RAMADDMASK;
            addr>>=1;
            ram[addr]=data&0xFFFF;
            return;
    }
    fprintf(stderr,"write16(0x%08X,0x%04X), abort pc 0x%04X\n",addr,data,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
void write32 ( uint32_t addr, uint32_t data )
{
if(DBUG) fprintf(stderr,"write32(0x%08X,0x%08X)\n",addr,data);
    switch(addr&0xF0000000)
    {
        case 0xF0000000: //halt
            dump_counters();
            exit(0);
        case 0xE0000000: //periph
            switch(addr)
            {
                case 0xE0000000:
if(diss) printf("uart: [");
                    printf("%c",data&0xFF);
if(diss) printf("]\n");
fflush(stdout);
                    break;

                case 0xE000E010:
                {
                    uint32_t old;

                    old=systick_ctrl;
                    systick_ctrl = data&0x00010007;
                    if(((old&1)==0)&&(systick_ctrl&1))
                    {
                        //timer started, load count
                        systick_count=systick_reload;
                    }
                    break;
                }
                case 0xE000E014:
                {
                    systick_reload=data&0x00FFFFFF;
                    break;
                }
                case 0xE000E018:
                {
                    systick_count=data&0x00FFFFFF;
                    break;
                }
                case 0xE000E01C:
                {
                    systick_calibrate=data&0x00FFFFFF;
                    break;
                }
            }
            return;
        case 0xD0000000: //debug
            switch(addr&0xFF)
            {
                case 0x00:
                {
                    fprintf(stderr,"[0x%08X][0x%08X] 0x%08X\n",read_register(14),addr,data);
                    return;
                }
                case 0x10:
                {
                    printf("0x%08X ",data);
                    return;
                }
                case 0x20:
                {
                    printf("0x%08X\n",data);
                    return;
                }
            }
        case SRAMBASE: //RAM
if(DBUGRAMW) fprintf(stderr,"write32(0x%08X,0x%08X)\n",addr,data);
            write16(addr+0,(data>> 0)&0xFFFF);
            write16(addr+2,(data>>16)&0xFFFF);
            return;
    }
    fprintf(stderr,"write32(0x%08X,0x%08X), abort pc 0x%04X\n",addr,data,read_register(15));
    exit(1);
}
//-----------------------------------------------------------------
//-------------------------------------------------------------------
uint32_t read16 ( uint32_t addr )
{
    uint32_t data;

    reads++;

if(DBUG) fprintf(stderr,"read16(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case 0x00000000: //ROM
            addr&=ROMADDMASK;
            addr>>=1;
            data=rom[addr];
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
        case SRAMBASE: //RAM
if(DBUGRAM) fprintf(stderr,"read16(0x%08X)=",addr);
            addr&=RAMADDMASK;
            addr>>=1;
            data=ram[addr];
if(DBUG) fprintf(stderr,"0x%04X\n",data);
if(DBUGRAM) fprintf(stderr,"0x%04X\n",data);
            return(data);
    }
    fprintf(stderr,"read16(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
uint32_t read32 ( uint32_t addr )
{
    uint32_t data;

if(DBUG) fprintf(stderr,"read32(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case 0x00000000: //ROM
        case SRAMBASE: //RAM
if(DBUGRAMW) fprintf(stderr,"read32(0x%08X)=",addr);
            data =read16(addr+0);
            data|=((uint32_t)read16(addr+2))<<16;
if(DBUG) fprintf(stderr,"0x%08X\n",data);
if(DBUGRAMW) fprintf(stderr,"0x%08X\n",data);
            return(data);
        case 0xE0000000:
        {
            switch(addr)
            {
                case 0xE000E010:
                {
                    data = systick_ctrl;
                    systick_ctrl&=(~0x00010000);
                    return(data);
                }
                case 0xE000E014:
                {
                    data=systick_reload;
                    return(data);
                }
                case 0xE000E018:
                {
                    data=systick_count;
                    return(data);
                }
                case 0xE000E01C:
                {
                    data=systick_calibrate;
                    return(data);
                }
            }
        }
    }
    fprintf(stderr,"read32(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}

//-------------------------------------------------------------------
//-------------------------------------------------------------------
// Register & Status flag Operations  
//-------------------------------------------------------------------
//-------------------------------------------------------------------

//-------------------------------------------------------------------
//-------------------------------------------------------------------
uint32_t read_register ( uint32_t reg )
{
    uint32_t data;

    reg&=0xF;
if(DBUG) fprintf(stderr,"read_register(%u)=",reg);
if(DBUGREG) fprintf(stderr,"read_register(%u)=",reg);
    data=reg_norm[reg];
    if(reg==15)
    {
        if(data&1)
        {
            fprintf(stderr,"pc has lsbit set 0x%08X\n",data);
        }
        data&=~1;
    }
if(DBUG) fprintf(stderr,"0x%08X\n",data);
if(DBUGREG) fprintf(stderr,"0x%08X\n",data);
    return(data);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
void write_register ( uint32_t reg, uint32_t data )
{
    reg&=0xF;
if(DBUG) fprintf(stderr,"write_register(%u,0x%08X)\n",reg,data);
if(DBUGREG) fprintf(stderr,"write_register(%u,0x%08X)\n",reg,data);
    if(reg==15) data&=~1;
    reg_norm[reg]=data;

if(output_vcd)
{
    unsigned int vv;
    fprintf(fpvcd,"b");
    for(vv=0x80000000;vv;vv>>=1)
    {
        if(vv&data) fprintf(fpvcd,"1"); else fprintf(fpvcd,"0");
    }
    fprintf(fpvcd," r%u\n",reg);
}

}

//-------------------------------------------------------------------
// Special register encoding for MSR instruction.
//-------------------------------------------------------------------
uint32_t get_SYSm(int index) {
  switch(index) {
    case 0: // APSR 
      return( cpsr & CPSR_APSR );
    default:
      fprintf(stderr,"Unsupported get_SYSm! %d\n",index);
      exit(-1);
      return(0);
  }
}


void set_SYSm(int index, uint32_t val) {
  switch(index) {
    case 0: // APSR 
      cpsr = (cpsr & ~(CPSR_APSR)) | (val & CPSR_APSR ) ;
      return;
    default:
      fprintf(stderr,"Unsupported set_SYSm! %d\n",index);
      exit(-1);
      return;
  }
}

//-------------------------------------------------------------------
void do_zflag ( uint32_t x )
{
    if(x==0) cpsr|=CPSR_Z; else cpsr&=~CPSR_Z;
}
//-------------------------------------------------------------------
void do_nflag ( uint32_t x )
{
    if(x&0x80000000) cpsr|=CPSR_N; else cpsr&=~CPSR_N;
}
//-------------------------------------------------------------------
void do_cflag ( uint32_t a, uint32_t b, uint32_t c )
{
    uint32_t rc;

    cpsr&=~CPSR_C;
    rc=(a&0x7FFFFFFF)+(b&0x7FFFFFFF)+c; //carry in
    rc = (rc>>31)+(a>>31)+(b>>31);  //carry out
    if(rc&2) cpsr|=CPSR_C;
}
//-------------------------------------------------------------------
void do_vflag ( uint32_t a, uint32_t b, uint32_t c )
{
    uint32_t rc;
    uint32_t rd;

    cpsr&=~CPSR_V;
    rc=(a&0x7FFFFFFF)+(b&0x7FFFFFFF)+c; //carry in
    rc>>=31; //carry in in lsbit
    rd=(rc&1)+((a>>31)&1)+((b>>31)&1); //carry out
    rd>>=1; //carry out in lsbit
    rc=(rc^rd)&1; //if carry in != carry out then signed overflow
    if(rc) cpsr|=CPSR_V;
}
//-------------------------------------------------------------------
void do_cflag_bit ( uint32_t x )
{
   if(x) cpsr|=CPSR_C; else cpsr&=~CPSR_C;
}
//-------------------------------------------------------------------
void do_vflag_bit ( uint32_t x )
{
   if(x) cpsr|=CPSR_V; else cpsr&=~CPSR_V;
}


//-------------------------------------------------------------------
//-------------------------------------------------------------------
// Register Stack/Unstack for exception support
//-------------------------------------------------------------------
//-------------------------------------------------------------------

// -----------------------------------------------

// Stack pointer and PC as arguments, return new sp value.
uint32_t exception_stack(uint32_t sp, uint32_t pc)
{
  sp-=4; write32(sp,cpsr);
  sp-=4; write32(sp,pc);
  sp-=4; write32(sp,read_register(reg_lr));
  sp-=4; write32(sp,read_register(reg_ip));
  sp-=4; write32(sp,read_register(reg_r3));
  sp-=4; write32(sp,read_register(reg_r2));
  sp-=4; write32(sp,read_register(reg_r1));
  sp-=4; write32(sp,read_register(reg_r0));
  return(sp);  
}

// Stack pointer as an arg, update the PC with the saved value.
uint32_t exception_unstack(uint32_t sp, uint32_t *pc)
{
  write_register(reg_r0,read32(sp)); sp+=4;
  write_register(reg_r1,read32(sp)); sp+=4;
  write_register(reg_r2,read32(sp)); sp+=4;
  write_register(reg_r3,read32(sp)); sp+=4;
  write_register(reg_ip,read32(sp)); sp+=4;
  write_register(reg_lr,read32(sp)); sp+=4;
  *pc=read32(sp); sp+=4;
  cpsr=read32(sp); sp+=4;
  return(sp);
}


//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------
// The main decoder 
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------

// -------------------------------------------------------------
// Notes on decode.
// Instruction set decode is described in the 
// ARMv6-M Architecture Reference Manual
// ARM DDI 0419D
//
// Refer to Chapter A5, The Thumb Instruction Set Encoding
// 

int execute ( void )
{
    uint32_t pc;
    uint32_t sp;
    uint32_t inst;
    uint32_t inst2; // Second half of a 32-bit instruction
    

    uint32_t ra,rb,rc;
    uint32_t rm,rd,rn,rs;
    uint32_t op;

//if(fetches>400000) return(1);

    pc=read_register(15);

    if(handler_mode)
    {
        if((pc&0xF0000000)==0xF0000000)
        {
            uint32_t sp;

            handler_mode = 0;
//fprintf(stderr,"--leaving handler\n");
          sp=read_register(reg_sp);
          sp = exception_unstack(sp, &pc);            
          write_register(reg_sp,sp);
        }
    }
    if(systick_ctrl&1)
    {
        if(systick_count)
        {
            systick_count--;
        }
        else
        {
            systick_count=systick_reload;
            systick_ctrl|=0x00010000;
        }
    }

    if((systick_ctrl&3)==3)
    {
        if(systick_ctrl&0x00010000)
        {
            if(handler_mode==0)
            {
                uint32_t sp;

                systick_ints++;
//fprintf(stderr,"--- enter systick handler\n");
                sp=read_register(reg_sp);
                sp = exception_stack(sp, pc); // Correct for order of operations.

                write_register(reg_sp,sp);
                pc=fetch32(0x0000003C); //systick vector
                pc+=2;
                //write_register(14,0xFFFFFF00);
                write_register(14,0xFFFFFFF9);

                handler_mode=1;
            }
        }
    }



    // -----------------------------------------------
    // -----------------------------------------------
    // Instruction Fetch
    inst=fetch16(pc-2);
    pc+=2;
    write_register(reg_pc,pc);
    
    instructions++;
    
    // Check for a 32-bit instruction.   Not all of these are 
    // defined.  See table A5-9 of the Arm-ARM
    if ( (inst & 0xF800) == 0xF000  ) {
        inst2=fetch16(pc-2);        
    }

    // -----------------------------------------------
    // -----------------------------------------------
    
    // Check to see if its a 32-bit instruction, and if so,
    // display the next word.  Arm V6-ARM, section A5.1
    if(diss) 
    {
      uint8_t topcode = (inst & 0xf800)>>8; // Top 5 bits
      if ( topcode == 0xF8 || topcode == 0xF0 || topcode == 0xE8) {
        uint16_t inst_top = fetch16(pc-2);
        fprintf(stderr,"--- 0x%08X: 0x%04X %04X ",(pc-4),inst,inst_top);
        }
      else {
        fprintf(stderr,"--- 0x%08X: 0x%04X ",(pc-4),inst);
        }
    }
    
if(output_vcd)
{
    unsigned int vv;
    fprintf(fpvcd,"b");
    for(vv=0x8000;vv;vv>>=1)
    {
        if(vv&inst) fprintf(fpvcd,"1"); else fprintf(fpvcd,"0");
    }
    fprintf(fpvcd," inst\n");
}

    //ADC
    if((inst&0xFFC0)==0x4140)
    {
        rd=(inst>>0)&0x07;
        rm=(inst>>3)&0x07;
if(diss) fprintf(stderr,"adc r%u,r%u\n",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra+rb;
        if(cpsr&CPSR_C) rc++;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        if(cpsr&CPSR_C) { do_cflag(ra,rb,1); do_vflag(ra,rb,1); }
        else            { do_cflag(ra,rb,0); do_vflag(ra,rb,0); }
        return(0);
    }

    //ADD(1) small immediate two registers
    if((inst&0xFE00)==0x1C00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rb=(inst>>6)&0x7;
        if(rb)
        {
if(diss) fprintf(stderr,"adds r%u,r%u,#0x%X\n",rd,rn,rb);
            ra=read_register(rn);
            rc=ra+rb;
//fprintf(stderr,"0x%08X = 0x%08X + 0x%08X\n",rc,ra,rb);
            write_register(rd,rc);
            do_nflag(rc);
            do_zflag(rc);
            do_cflag(ra,rb,0);
            do_vflag(ra,rb,0);
            return(0);
        }
        else
        {
            //this is a mov
        }
    }

    //ADD(2) big immediate one register
    if((inst&0xF800)==0x3000)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x7;
if(diss) fprintf(stderr,"adds r%u,#0x%02X\n",rd,rb);
        ra=read_register(rd);
        rc=ra+rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,rb,0);
        do_vflag(ra,rb,0);
        return(0);
    }

    //ADD(3) three registers
    if((inst&0xFE00)==0x1800)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"adds r%u,r%u,r%u\n",rd,rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra+rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,rb,0);
        do_vflag(ra,rb,0);
        return(0);
    }

    //ADD(4) two registers one or both high no flags
    if((inst&0xFF00)==0x4400)
    {
        if((inst>>6)&3)
        {
            //UNPREDICTABLE
        }
        rd=(inst>>0)&0x7;
        rd|=(inst>>4)&0x8;
        rm=(inst>>3)&0xF;
if(diss) fprintf(stderr,"add r%u,r%u\n",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra+rb;
        if(rd==15)
        {
            if((rc&1)==0)
            {
                fprintf(stderr,"add pc,... produced an arm address 0x%08X 0x%08X\n",pc,rc);
                exit(1);
            }
            rc&=~1; //write_register may do this as well
            rc+=2; //The program counter is special
        }
//fprintf(stderr,"0x%08X = 0x%08X + 0x%08X\n",rc,ra,rb);
        write_register(rd,rc);
        return(0);
    }

    //ADD(5) rd = pc plus immediate
    if((inst&0xF800)==0xA000)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x7;
        rb<<=2;
if(diss) fprintf(stderr,"add r%u,PC,#0x%02X\n",rd,rb);
        ra=read_register(15);
        rc=(ra&(~3))+rb;
        write_register(rd,rc);
        return(0);
    }

    //ADD(6) rd = sp plus immediate
    if((inst&0xF800)==0xA800)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x7;
        rb<<=2;
if(diss) fprintf(stderr,"add r%u,SP,#0x%02X\n",rd,rb);
        ra=read_register(13);
        rc=ra+rb;
        write_register(rd,rc);
        return(0);
    }

    //ADD(7) sp plus immediate
    if((inst&0xFF80)==0xB000)
    {
        rb=(inst>>0)&0x7F;
        rb<<=2;
if(diss) fprintf(stderr,"add SP,#0x%02X\n",rb);
        ra=read_register(13);
        rc=ra+rb;
        write_register(13,rc);
        return(0);
    }

    //AND
    if((inst&0xFFC0)==0x4000)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"ands r%u,r%u\n",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra&rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //ASR(1) two register immediate
    if((inst&0xF800)==0x1000)
    {
        rd=(inst>>0)&0x07;
        rm=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(diss) fprintf(stderr,"asrs r%u,r%u,#0x%X\n",rd,rm,rb);
        rc=read_register(rm);
        if(rb==0)
        {
            if(rc&0x80000000)
            {
                do_cflag_bit(1);
                rc=~0;
            }
            else
            {
                do_cflag_bit(0);
                rc=0;
            }
        }
        else
        {
            do_cflag_bit(rc&(1<<(rb-1)));
            ra=rc&0x80000000;
            rc>>=rb;
            if(ra) //asr, sign is shifted in
            {
                rc|=(~0)<<(32-rb);
            }
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //ASR(2) two register
    if((inst&0xFFC0)==0x4100)
    {
        rd=(inst>>0)&0x07;
        rs=(inst>>3)&0x07;
if(diss) fprintf(stderr,"asrs r%u,r%u\n",rd,rs);
        rc=read_register(rd);
        rb=read_register(rs);
        rb&=0xFF;
        if(rb==0)
        {
        }
        else if(rb<32)
        {
            do_cflag_bit(rc&(1<<(rb-1)));
            ra=rc&0x80000000;
            rc>>=rb;
            if(ra) //asr, sign is shifted in
            {
                rc|=(~0)<<(32-rb);
            }
        }
        else
        {
            if(rc&0x80000000)
            {
                do_cflag_bit(1);
                rc=(~0);
            }
            else
            {
                do_cflag_bit(0);
                rc=0;
            }
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //B(1) conditional branch
    if((inst&0xF000)==0xD000)
    {
        rb=(inst>>0)&0xFF;
        if(rb&0x80) rb|=(~0)<<8;
        op=(inst>>8)&0xF;
        rb<<=1;
        rb+=pc;
        rb+=2;
        switch(op)
        {
            case 0x0: //b eq  z set
if(diss) fprintf(stderr,"beq 0x%08X\n",rb-2);
                if(cpsr&CPSR_Z)
                {
                    write_register(15,rb);
                }
                return(0);

            case 0x1: //b ne  z clear
if(diss) fprintf(stderr,"bne 0x%08X\n",rb-2);
                if(!(cpsr&CPSR_Z))
                {
                    write_register(15,rb);
                }
                return(0);

            case 0x2: //b cs c set
if(diss) fprintf(stderr,"bcs 0x%08X\n",rb-2);
                if(cpsr&CPSR_C)
                {
                    write_register(15,rb);
                }
                return(0);

            case 0x3: //b cc c clear
if(diss) fprintf(stderr,"bcc 0x%08X\n",rb-2);
                if(!(cpsr&CPSR_C))
                {
                    write_register(15,rb);
                }
                return(0);

            case 0x4: //b mi n set
if(diss) fprintf(stderr,"bmi 0x%08X\n",rb-2);
                if(cpsr&CPSR_N)
                {
                    write_register(15,rb);
                }
                return(0);

            case 0x5: //b pl n clear
if(diss) fprintf(stderr,"bpl 0x%08X\n",rb-2);
                if(!(cpsr&CPSR_N))
                {
                    write_register(15,rb);
                }
                return(0);


            case 0x6: //b vs v set
if(diss) fprintf(stderr,"bvs 0x%08X\n",rb-2);
                if(cpsr&CPSR_V)
                {
                    write_register(15,rb);
                }
                return(0);

            case 0x7: //b vc v clear
if(diss) fprintf(stderr,"bvc 0x%08X\n",rb-2);
                if(!(cpsr&CPSR_V))
                {
                    write_register(15,rb);
                }
                return(0);


            case 0x8: //b hi c set z clear
if(diss) fprintf(stderr,"bhi 0x%08X\n",rb-2);
                if((cpsr&CPSR_C)&&(!(cpsr&CPSR_Z)))
                {
                    write_register(15,rb);
                }
                return(0);

            case 0x9: //b ls c clear or z set
if(diss) fprintf(stderr,"bls 0x%08X\n",rb-2);
                if((cpsr&CPSR_Z)||(!(cpsr&CPSR_C)))
                {
                    write_register(15,rb);
                }
                return(0);

            case 0xA: //b ge N == V
if(diss) fprintf(stderr,"bge 0x%08X\n",rb-2);
                ra=0;
                if(  (cpsr&CPSR_N) &&  (cpsr&CPSR_V) ) ra++;
                if((!(cpsr&CPSR_N))&&(!(cpsr&CPSR_V))) ra++;
                if(ra)
                {
                    write_register(15,rb);
                }
                return(0);

            case 0xB: //b lt N != V
if(diss) fprintf(stderr,"blt 0x%08X\n",rb-2);
                ra=0;
                if((!(cpsr&CPSR_N))&&(cpsr&CPSR_V)) ra++;
                if((!(cpsr&CPSR_V))&&(cpsr&CPSR_N)) ra++;
                if(ra)
                {
                    write_register(15,rb);
                }
                return(0);

            case 0xC: //b gt Z==0 and N == V
if(diss) fprintf(stderr,"bgt 0x%08X\n",rb-2);
                ra=0;
                if(  (cpsr&CPSR_N) &&  (cpsr&CPSR_V) ) ra++;
                if((!(cpsr&CPSR_N))&&(!(cpsr&CPSR_V))) ra++;
                if(cpsr&CPSR_Z) ra=0;
                if(ra)
                {
                    write_register(15,rb);
                }
                return(0);

            case 0xD: //b le Z==1 or N != V
if(diss) fprintf(stderr,"ble 0x%08X\n",rb-2);
                ra=0;
                if((!(cpsr&CPSR_N))&&(cpsr&CPSR_V)) ra++;
                if((!(cpsr&CPSR_V))&&(cpsr&CPSR_N)) ra++;
                if(cpsr&CPSR_Z) ra++;
                if(ra)
                {
                    write_register(15,rb);
                }
                return(0);

            case 0xE:
                //undefined instruction
                break;
            case 0xF:
                //swi
                break;
        }
    }

    //B(2) unconditional branch
    if((inst&0xF800)==0xE000)
    {
        rb=(inst>>0)&0x7FF;
        if(rb&(1<<10)) rb|=(~0)<<11;
        rb<<=1;
        rb+=pc;
        rb+=2;
if(diss) fprintf(stderr,"B 0x%08X\n",rb-2);
        write_register(15,rb);
        return(0);
    }

    //BIC
    if((inst&0xFFC0)==0x4380)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"bics r%u,r%u\n",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra&(~rb);
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //BKPT
    if((inst&0xFF00)==0xBE00)
    {
        rb=(inst>>0)&0xFF;
        fprintf(stderr,"bkpt 0x%02X\n",rb);
        return(1);
    }

    // BL is a 32-bit instruction.
    if( ((inst&0xF800)==0xF000) && ((inst2&0x5000) == 0x5000) ) //BL
    {
      // Advance the program counter.   We already have both halves
      // in-hand.  
      pc+=2;
      
      // A6.7.13 Refers to J1 & J2
      // Start by capturing the bottom 21 bits of offset, shift to 22.
      uint32_t offset = ((inst & 0x3FF )<<12) | ((inst2&0x7FF)<<1);

      uint32_t sign = ( (inst&0x0400) != 0) ? 1 : 0; 

      // I1 is bit #23 
      uint32_t i1 = ( (inst2&0x2000) != 0) ? 1 : 0; 
      i1 = ~(sign ^ i1) & 1 ;

      offset |= i1<<23; // Install I1  

      // I2 is bit #22 
      uint32_t i2 = ( (inst2&0x0800) != 0) ? 1 : 0; 
      i2 = ~(sign ^ i2) & 1;

      offset |= i2<<22; // Install I2  

      // We now have 22 bits of address, and we need the sign bits.
      if ( sign ) offset |= 0xFF000000;
      
      if(diss) fprintf(stderr,"BL 0x%x\n",pc + offset - 2);

      write_register(reg_lr,(pc-2)|1);
      write_register(reg_pc,(pc + offset));
      return(0);
    }
    

    //BLX(2)
    if((inst&0xFF87)==0x4780)
    {
        rm=(inst>>3)&0xF;
if(diss) fprintf(stderr,"blx r%u\n",rm);
        rc=read_register(rm);
//fprintf(stderr,"blx r%u 0x%X 0x%X\n",rm,rc,pc);
        rc+=2;
        if(rc&1)
        {
            write_register(14,(pc-2)|1);
            rc&=~1;
            write_register(15,rc);
            return(0);
        }
        else
        {
            fprintf(stderr,"cannot branch to arm 0x%08X 0x%04X\n",pc,inst);
            return(1);
        }
    }

    //BX
    if((inst&0xFF87)==0x4700)
    {
        rm=(inst>>3)&0xF;
if(diss) fprintf(stderr,"bx r%u\n",rm);
        rc=read_register(rm);
        rc+=2;
//fprintf(stderr,"bx r%u 0x%X 0x%X\n",rm,rc,pc);
        if(rc&1)
        {
            rc&=~1;
            write_register(15,rc);
            return(0);
        }
        else
        {
            fprintf(stderr,"cannot branch to arm 0x%08X 0x%04X\n",pc,inst);
            return(1);
        }
    }

    //CMN
    if((inst&0xFFC0)==0x42C0)
    {
        rn=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"cmns r%u,r%u\n",rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra+rb;
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,rb,0);
        do_vflag(ra,rb,0);
        return(0);
    }

    //CMP(1) compare immediate
    if((inst&0xF800)==0x2800)
    {
        rb=(inst>>0)&0xFF;
        rn=(inst>>8)&0x07;
if(diss) fprintf(stderr,"cmp r%u,#0x%02X\n",rn,rb);
        ra=read_register(rn);
        rc=ra-rb;
//fprintf(stderr,"0x%08X 0x%08X\n",ra,rb);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        return(0);
    }

    //CMP(2) compare register
    if((inst&0xFFC0)==0x4280)
    {
        rn=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"cmps r%u,r%u\n",rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra-rb;
//fprintf(stderr,"0x%08X 0x%08X\n",ra,rb);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        return(0);
    }

    //CMP(3) compare high register
    if((inst&0xFF00)==0x4500)
    {
        if(((inst>>6)&3)==0x0)
        {
            //UNPREDICTABLE
        }
        rn=(inst>>0)&0x7;
        rn|=(inst>>4)&0x8;
        if(rn==0xF)
        {
            //UNPREDICTABLE
        }
        rm=(inst>>3)&0xF;
if(diss) fprintf(stderr,"cmps r%u,r%u\n",rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra-rb;
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        return(0);
    }

    //CPS
    if((inst&0xFFE8)==0xB660)
    {
if(diss) fprintf(stderr,"cps NOTSUPPORTED\n");
        return(0);
    }

    //CPY copy high register
    if((inst&0xFFC0)==0x4600)
    {
        //same as mov except you can use both low registers
        //going to let mov handle high registers
        rd=(inst>>0)&0x7; //mov handles the high registers
        rm=(inst>>3)&0x7; //mov handles the high registers
if(diss) fprintf(stderr,"cpy r%u,r%u\n",rd,rm);
        rc=read_register(rm);
        //if(rd==15) //mov handles the high registers like r15
        //{
            //rc&=~1;
            //rc+=2; //The program counter is special
        //}
        write_register(rd,rc);
        return(0);
    }

    //EOR
    if((inst&0xFFC0)==0x4040)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"eors r%u,r%u\n",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra^rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //LDMIA
    if((inst&0xF800)==0xC800)
    {
        rn=(inst>>8)&0x7;
if(diss)
{
    fprintf(stderr,"ldmia r%u!,{",rn);
    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(inst&rb)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"r%u",ra);
            rc++;
        }
    }
    fprintf(stderr,"}\n");
}
        sp=read_register(rn);
        for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                write_register(ra,read32(sp));
                sp+=4;
            }
        }
        //there is a write back exception.
        if((inst&(1<<rn))==0) write_register(rn,sp);
        return(0);
    }

    //LDR(1) two register immediate
    if((inst&0xF800)==0x6800)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
        rb<<=2;
if(diss) fprintf(stderr,"ldr r%u,[r%u,#0x%X]\n",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read32(rb);
        write_register(rd,rc);
        return(0);
    }

    //LDR(2) three register
    if((inst&0xFE00)==0x5800)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"ldr r%u,[r%u,r%u]\n",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read32(rb);
        write_register(rd,rc);
        return(0);
    }

    //LDR(3)
    if((inst&0xF800)==0x4800)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
        rb<<=2;
if(diss) fprintf(stderr,"ldr r%u,[PC+#0x%X] ",rd,rb);
        ra=read_register(15);
        ra&=~3;
        rb+=ra;
if(diss) fprintf(stderr,";@ 0x%X\n",rb);
        rc=read32(rb);
        write_register(rd,rc);
        return(0);
    }

    //LDR(4)
    if((inst&0xF800)==0x9800)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
        rb<<=2;
if(diss) fprintf(stderr,"ldr r%u,[SP+#0x%X]\n",rd,rb);
        ra=read_register(13);
        //ra&=~3;
        rb+=ra;
        rc=read32(rb);
        write_register(rd,rc);
        return(0);
    }

    //LDRB(1)
    if((inst&0xF800)==0x7800)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(diss) fprintf(stderr,"ldrb r%u,[r%u,#0x%X]\n",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read16(rb&(~1));
        if(rb&1)
        {
            rc>>=8;
        }
        else
        {
        }
        write_register(rd,rc&0xFF);
        return(0);
    }

    //LDRB(2)
    if((inst&0xFE00)==0x5C00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"ldrb r%u,[r%u,r%u]\n",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read16(rb&(~1));
        if(rb&1)
        {
            rc>>=8;
        }
        else
        {
        }
        write_register(rd,rc&0xFF);
        return(0);
    }

    //LDRH(1)
    if((inst&0xF800)==0x8800)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
        rb<<=1;
if(diss) fprintf(stderr,"ldrh r%u,[r%u,#0x%X]\n",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read16(rb);
        write_register(rd,rc&0xFFFF);
        return(0);
    }

    //LDRH(2)
    if((inst&0xFE00)==0x5A00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"ldrh r%u,[r%u,r%u]\n",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read16(rb);
        write_register(rd,rc&0xFFFF);
        return(0);
    }

    //LDRSB
    if((inst&0xFE00)==0x5600)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"ldrsb r%u,[r%u,r%u]\n",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read16(rb&(~1));
        if(rb&1)
        {
            rc>>=8;
        }
        else
        {
        }
        rc&=0xFF;
        if(rc&0x80) rc|=((~0)<<8);
        write_register(rd,rc);
        return(0);
    }

    //LDRSH
    if((inst&0xFE00)==0x5E00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"ldrsh r%u,[r%u,r%u]\n",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read16(rb);
        rc&=0xFFFF;
        if(rc&0x8000) rc|=((~0)<<16);
        write_register(rd,rc);
        return(0);
    }

    //LSL(1)
    if((inst&0xF800)==0x0000)
    {
        rd=(inst>>0)&0x07;
        rm=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(diss) fprintf(stderr,"lsls r%u,r%u,#0x%X\n",rd,rm,rb);
        rc=read_register(rm);
        if(rb==0)
        {
            //if immed_5 == 0
            //C unnaffected
            //result not shifted
        }
        else
        {
            //else immed_5 > 0
            do_cflag_bit(rc&(1<<(32-rb)));
            rc<<=rb;
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //LSL(2) two register
    if((inst&0xFFC0)==0x4080)
    {
        rd=(inst>>0)&0x07;
        rs=(inst>>3)&0x07;
if(diss) fprintf(stderr,"lsls r%u,r%u\n",rd,rs);
        rc=read_register(rd);
        rb=read_register(rs);
        rb&=0xFF;
        if(rb==0)
        {
        }
        else if(rb<32)
        {
            do_cflag_bit(rc&(1<<(32-rb)));
            rc<<=rb;
        }
        else if(rb==32)
        {
            do_cflag_bit(rc&1);
            rc=0;
        }
        else
        {
            do_cflag_bit(0);
            rc=0;
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //LSR(1) two register immediate
    if((inst&0xF800)==0x0800)
    {
        rd=(inst>>0)&0x07;
        rm=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(diss) fprintf(stderr,"lsrs r%u,r%u,#0x%X\n",rd,rm,rb);
        rc=read_register(rm);
        if(rb==0)
        {
            do_cflag_bit(rc&0x80000000);
            rc=0;
        }
        else
        {
            do_cflag_bit(rc&(1<<(rb-1)));
            rc>>=rb;
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //LSR(2) two register
    if((inst&0xFFC0)==0x40C0)
    {
        rd=(inst>>0)&0x07;
        rs=(inst>>3)&0x07;
if(diss) fprintf(stderr,"lsrs r%u,r%u\n",rd,rs);
        rc=read_register(rd);
        rb=read_register(rs);
        rb&=0xFF;
        if(rb==0)
        {
        }
        else if(rb<32)
        {
            do_cflag_bit(rc&(1<<(rb-1)));
            rc>>=rb;
        }
        else if(rb==32)
        {
            do_cflag_bit(rc&0x80000000);
            rc=0;
        }
        else
        {
            do_cflag_bit(0);
            rc=0;
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //MOV(1) immediate
    if((inst&0xF800)==0x2000)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
if(diss) fprintf(stderr,"movs r%u,#0x%02X\n",rd,rb);
        write_register(rd,rb);
        do_nflag(rb);
        do_zflag(rb);
        return(0);
    }

    //MOV(2) two low registers
    if((inst&0xFFC0)==0x1C00)
    {
        rd=(inst>>0)&7;
        rn=(inst>>3)&7;
if(diss) fprintf(stderr,"movs r%u,r%u\n",rd,rn);
        rc=read_register(rn);
//fprintf(stderr,"0x%08X\n",rc);
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag_bit(0);
        do_vflag_bit(0);
        return(0);
    }

    //MOV(3)
    if((inst&0xFF00)==0x4600)
    {
        rd=(inst>>0)&0x7;
        rd|=(inst>>4)&0x8;
        rm=(inst>>3)&0xF;
if(diss) fprintf(stderr,"mov r%u,r%u\n",rd,rm);
        rc=read_register(rm);
        if((rd==14)&&(rm==15))
        {
            //printf("mov lr,pc warning 0x%08X\n",pc-2);
            //rc|=1;
        }
        if(rd==15)
        {
            //if((rc&1)==0)
            //{
                //fprintf(stderr,"cpy or mov pc,... produced an ARM address 0x%08X 0x%08X\n",pc,rc);
                //exit(1);
            //}
            rc&=~1; //write_register may do this as well
            rc+=2; //The program counter is special
        }
        write_register(rd,rc);
        return(0);
    }

    // MRS 0xF800 | 0x07F0 op1 = 011111x ( 0x03e0 )
    // 
    if( ((inst&0xFFF0) == 0xF3e0) && ((inst2&0x5000) == 0) ) {
        rd = (inst2>>8)&0xf;
        uint32_t SYSm = inst2&0xff;
        write_register(rd, get_SYSm(SYSm));
        if(diss) fprintf(stderr,"mrs r%d = %x(SYS%d)\n",rd, get_SYSm(SYSm), SYSm);
        pc +=2; // Advance over the consumed top half
        write_register(reg_pc,pc);
	      return(0);
	  }

    // MSR 
    if( ((inst&0xFFF0) == 0xF380) && ((inst2&0x5000) == 0) ) {
      rd = inst&0xf;
      uint32_t SYSm = inst2&0xff;
      if(diss) fprintf(stderr,"msr SYS%d = %x(r%d)\n",SYSm, read_register(rd), rd);
      set_SYSm(SYSm, read_register(rd));
      pc +=2; // Advance over the consumed top half
      write_register(reg_pc,pc);
      return(0);
	   }

    //MUL
    if((inst&0xFFC0)==0x4340)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"muls r%u,r%u\n",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra*rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //MVN
    if((inst&0xFFC0)==0x43C0)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"mvns r%u,r%u\n",rd,rm);
        ra=read_register(rm);
        rc=(~ra);
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //NEG
    if((inst&0xFFC0)==0x4240)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"negs r%u,r%u\n",rd,rm);
        ra=read_register(rm);
        rc=0-ra;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(0,~ra,1);
        do_vflag(0,~ra,1);
        return(0);
    }

    //ORR
    if((inst&0xFFC0)==0x4300)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"orrs r%u,r%u\n",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra|rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }


    //POP
    if((inst&0xFE00)==0xBC00)
    {
if(diss)
{
    fprintf(stderr,"pop {");
    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(inst&rb)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"r%u",ra);
            rc++;
        }
    }
    if(inst&0x100)
    {
        if(rc) fprintf(stderr,",");
        fprintf(stderr,"pc");
    }
    fprintf(stderr,"}\n");
}

        sp=read_register(13);
        for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                write_register(ra,read32(sp));
                sp+=4;
            }
        }
        if(inst&0x100)
        {
            rc=read32(sp);
            if((rc&1)==0)
            {
                fprintf(stderr,"pop {rc} with an ARM address pc 0x%08X popped 0x%08X\n",pc,rc);
                //exit(1);
                rc&=~1;
            }
            rc+=2;
            write_register(15,rc);
            sp+=4;
        }
        write_register(13,sp);
        return(0);
    }

    //PUSH
    if((inst&0xFE00)==0xB400)
    {

if(diss)
{
    fprintf(stderr,"push {");
    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(inst&rb)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"r%u",ra);
            rc++;
        }
    }
    if(inst&0x100)
    {
        if(rc) fprintf(stderr,",");
        fprintf(stderr,"lr");
    }
    fprintf(stderr,"}\n");
}

        sp=read_register(13);
//fprintf(stderr,"sp 0x%08X\n",sp);
        for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                rc++;
            }
        }
        if(inst&0x100) rc++;
        rc<<=2;
        sp-=rc;
        rd=sp;
        for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                write32(rd,read_register(ra));
                rd+=4;
            }
        }
        if(inst&0x100)
        {
            rc=read_register(14);
            write32(rd,rc); //read_register(14));

            if((rc&1)==0)
            {
                fprintf(stderr,"push {lr} with an ARM address pc 0x%08X popped 0x%08X\n",pc,rc);
//                exit(1);
            }


        }
        write_register(13,sp);
        return(0);
    }

    //REV
    if((inst&0xFFC0)==0xBA00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
if(diss) fprintf(stderr,"rev r%u,r%u\n",rd,rn);
        ra=read_register(rn);
        rc =((ra>> 0)&0xFF)<<24;
        rc|=((ra>> 8)&0xFF)<<16;
        rc|=((ra>>16)&0xFF)<< 8;
        rc|=((ra>>24)&0xFF)<< 0;
        write_register(rd,rc);
        return(0);
    }

    //REV16
    if((inst&0xFFC0)==0xBA40)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
if(diss) fprintf(stderr,"rev16 r%u,r%u\n",rd,rn);
        ra=read_register(rn);
        rc =((ra>> 0)&0xFF)<< 8;
        rc|=((ra>> 8)&0xFF)<< 0;
        rc|=((ra>>16)&0xFF)<<24;
        rc|=((ra>>24)&0xFF)<<16;
        write_register(rd,rc);
        return(0);
    }

    //REVSH
    if((inst&0xFFC0)==0xBAC0)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
if(diss) fprintf(stderr,"revsh r%u,r%u\n",rd,rn);
        ra=read_register(rn);
        rc =((ra>> 0)&0xFF)<< 8;
        rc|=((ra>> 8)&0xFF)<< 0;
        if(rc&0x8000) rc|=0xFFFF0000;
        else          rc&=0x0000FFFF;
        write_register(rd,rc);
        return(0);
    }

    //ROR
    if((inst&0xFFC0)==0x41C0)
    {
        rd=(inst>>0)&0x7;
        rs=(inst>>3)&0x7;
if(diss) fprintf(stderr,"rors r%u,r%u\n",rd,rs);
        rc=read_register(rd);
        ra=read_register(rs);
        ra&=0xFF;
        if(ra==0)
        {
        }
        else
        {
            ra&=0x1F;
            if(ra==0)
            {
                do_cflag_bit(rc&0x80000000);
            }
            else
            {
                do_cflag_bit(rc&(1<<(ra-1)));
                rb=rc<<(32-ra);
                rc>>=ra;
                rc|=rb;
            }
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //SBC
    if((inst&0xFFC0)==0x4180)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"sbc r%u,r%u\n",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra-rb;
        if(!(cpsr&CPSR_C)) rc--;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        if(cpsr&CPSR_C)
        {
            do_cflag(ra,~rb,1);
            do_vflag(ra,~rb,1);
        }
        else
        {
            do_cflag(ra,~rb,0);
            do_vflag(ra,~rb,0);
        }
        return(0);
    }

    //SETEND
    if((inst&0xFFF7)==0xB650)
    {
        fprintf(stderr,"setend not implemented\n");
        return(1);
    }

    //STMIA
    if((inst&0xF800)==0xC000)
    {
        rn=(inst>>8)&0x7;

if(diss)
{
    fprintf(stderr,"stmia r%u!,{",rn);
    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(inst&rb)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"r%u",ra);
            rc++;
        }
    }
    fprintf(stderr,"}\n");
}
        sp=read_register(rn);
        for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                write32(sp,read_register(ra));
                sp+=4;
            }
        }
        write_register(rn,sp);
        return(0);
    }

    //STR(1)
    if((inst&0xF800)==0x6000)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
        rb<<=2;
if(diss) fprintf(stderr,"str r%u,[r%u,#0x%X]\n",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read_register(rd);
        write32(rb,rc);
        return(0);
    }

    //STR(2)
    if((inst&0xFE00)==0x5000)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"str r%u,[r%u,r%u]\n",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read_register(rd);
        write32(rb,rc);
        return(0);
    }

    //STR(3)
    if((inst&0xF800)==0x9000)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
        rb<<=2;
if(diss) fprintf(stderr,"str r%u,[SP,#0x%X]\n",rd,rb);
        rb=read_register(13)+rb;
//fprintf(stderr,"0x%08X\n",rb);
        rc=read_register(rd);
        write32(rb,rc);
        return(0);
    }

    //STRB(1)
    if((inst&0xF800)==0x7000)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(diss) fprintf(stderr,"strb r%u,[r%u,#0x%X]\n",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read_register(rd);
        ra=read16(rb&(~1));
        if(rb&1)
        {
            ra&=0x00FF;
            ra|=rc<<8;
        }
        else
        {
            ra&=0xFF00;
            ra|=rc&0x00FF;
        }
        write16(rb&(~1),ra&0xFFFF);
        return(0);
    }

    //STRB(2)
    if((inst&0xFE00)==0x5400)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"strb r%u,[r%u,r%u]\n",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read_register(rd);
        ra=read16(rb&(~1));
        if(rb&1)
        {
            ra&=0x00FF;
            ra|=rc<<8;
        }
        else
        {
            ra&=0xFF00;
            ra|=rc&0x00FF;
        }
        write16(rb&(~1),ra&0xFFFF);
        return(0);
    }

    //STRH(1)
    if((inst&0xF800)==0x8000)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
        rb<<=1;
if(diss) fprintf(stderr,"strh r%u,[r%u,#0x%X]\n",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read_register(rd);
        write16(rb,rc&0xFFFF);
        return(0);
    }

    //STRH(2)
    if((inst&0xFE00)==0x5200)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"strh r%u,[r%u,r%u]\n",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read_register(rd);
        write16(rb,rc&0xFFFF);
        return(0);
    }

    //SUB(1)
    if((inst&0xFE00)==0x1E00)
    {
        rd=(inst>>0)&7;
        rn=(inst>>3)&7;
        rb=(inst>>6)&7;
if(diss) fprintf(stderr,"subs r%u,r%u,#0x%X\n",rd,rn,rb);
        ra=read_register(rn);
        rc=ra-rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        return(0);
    }

    //SUB(2)
    if((inst&0xF800)==0x3800)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
if(diss) fprintf(stderr,"subs r%u,#0x%02X\n",rd,rb);
        ra=read_register(rd);
        rc=ra-rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        return(0);
    }

    //SUB(3)
    if((inst&0xFE00)==0x1A00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(diss) fprintf(stderr,"subs r%u,r%u,r%u\n",rd,rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra-rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        return(0);
    }

    //SUB(4)
    if((inst&0xFF80)==0xB080)
    {
        rb=inst&0x7F;
        rb<<=2;
if(diss) fprintf(stderr,"sub SP,#0x%02X\n",rb);
        ra=read_register(13);
        ra-=rb;
        write_register(13,ra);
        return(0);
    }

    //SVC/SWI
    if((inst&0xFF00)==0xDF00)
    {
        rb=inst&0xFF;

if(diss) fprintf(stderr,"svc/swi 0x%02X ",rb);

        if((inst&0xFF)==0xCC)
        {
            write_register(0,cpsr);
            return(0);
        }
        else
        {
            // fprintf(stderr,"\n\nsvc/swi 0x%02X\n",rb);
            
            sp=read_register(reg_sp);
            // Correct PC value so that it matches what we'd
            // see on an actual Cortex-M.  
            sp = exception_stack(sp, pc-2);
            write_register(reg_sp, sp);

            if ( diss ) {
              fprintf(stderr,"%08x: ", sp);
              for ( int i = sp ; i < (sp + 16); i = i + 4) {
                fprintf(stderr,"%08x ", fetch32(i));
              }
              fprintf(stderr,"\n");
            }
            // fprintf(stderr,"%08x: ", addr);

            // for ( int i = addr; i < (addr + 0x20); i = i + 4) {
            //  fprintf(stderr,"%08x ", fetch32(i));
            //}
            //fprintf(stderr,"\n");


            // An actual hander will dig through the stack for arguments 
            // and leave its result in there. 
            svc_handler(sp); 
            
            if(diss) {
              fprintf(stderr,"---                    svc/swi out: ");
              fprintf(stderr,"%08x: ", sp);
              for ( int i = sp ; i < (sp + 16); i = i + 4) {
                fprintf(stderr,"%08x ", fetch32(i));
              }
              fprintf(stderr,"\n");
            }
            
            sp=read_register(reg_sp);
            sp = exception_unstack(sp, &pc);
            write_register(reg_sp,sp);

            pc += 2;  // Adjust for thumbulator.
                        
            return(0);
        }
    }

    //SXTB
    if((inst&0xFFC0)==0xB240)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"sxtb r%u,r%u\n",rd,rm);
        ra=read_register(rm);
        rc=ra&0xFF;
        if(rc&0x80) rc|=(~0)<<8;
        write_register(rd,rc);
        return(0);
    }

    //SXTH
    if((inst&0xFFC0)==0xB200)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"sxth r%u,r%u\n",rd,rm);
        ra=read_register(rm);
        rc=ra&0xFFFF;
        if(rc&0x8000) rc|=(~0)<<16;
        write_register(rd,rc);
        return(0);
    }

    //TST
    if((inst&0xFFC0)==0x4200)
    {
        rn=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"tst r%u,r%u\n",rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra&rb;
        do_nflag(rc);
        do_zflag(rc);
        return(0);
    }

    //UXTB
    if((inst&0xFFC0)==0xB2C0)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"uxtb r%u,r%u\n",rd,rm);
        ra=read_register(rm);
        rc=ra&0xFF;
        write_register(rd,rc);
        return(0);
    }

    //UXTH
    if((inst&0xFFC0)==0xB280)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(diss) fprintf(stderr,"uxth r%u,r%u\n",rd,rm);
        ra=read_register(rm);
        rc=ra&0xFFFF;
        write_register(rd,rc);
        return(0);
    }

    // NOP - Compilers use this as padding.  
    if((inst&0xFF00)==0xBF00)
    {
      if(diss) fprintf(stderr,"nop\n");
      return(0);
    }  
  

    fprintf(stderr,"invalid instruction 0x%08X 0x%04X\n",pc-4,inst);
    return(1);
}
//-------------------------------------------------------------------
int reset ( void )
{
    memset(ram,0xFF,sizeof(ram));

    systick_ctrl=0x00000004;
    systick_reload=0x00000000;
    systick_count=0x00000000;
    systick_calibrate=0x00ABCDEF;
    handler_mode=0;
    cpsr=0;

    reg_norm[13]=fetch32(0x00000000); //cortex-m
    reg_norm[14]=0xFFFFFFFF;
    reg_norm[15]=fetch32(0x00000004); //cortex-m
    if((reg_norm[15]&1)==0)
    {
        fprintf(stderr,"reset vector with an ARM address 0x%08X\n",reg_norm[15]);
        exit(1);
    }
    reg_norm[15]&=~1;
    reg_norm[15]+=2;

    instructions=0;
    fetches=0;
    reads=0;
    writes=0;

    return(0);
}
//-------------------------------------------------------------------
int run ( void )
{
    reset();
    while(1)
    {
        if(output_vcd)
        {
            fprintf(fpvcd,"#%u\n",vcdcount++);
        }
        if(execute()) break;
    }
    
    if ( display_counters ) dump_counters();
    
    return(0);
}
//-------------------------------------------------------------------
int main ( int argc, char *argv[] )
{
    FILE *fp;

    uint32_t ra;

    if(argc<2)
    {
        fprintf(stderr,"bin file not specified\n");
        return(1);
    }

    output_vcd=0;
    for(ra=2;ra<argc;ra++)
    {
        if(strcmp(argv[ra],"--vcd")==0) output_vcd=1;
    }
    fp=fopen(argv[1],"rb");
    if(fp==NULL)
    {
        fprintf(stderr,"Error opening file [%s]\n",argv[1]);
        return(1);
    }
    memset(rom,0xFF,sizeof(rom));
    ra=fread(rom,1,sizeof(rom),fp);
    fclose(fp);

    if(output_vcd)
    {
        fprintf(stderr,"output vcd enabled\n");
        fpvcd=fopen("output.vcd","wt");
        if(fpvcd==NULL)
        {
            fprintf(stderr,"Error creating file output.vcd\n");
            output_vcd=0;
            return(1);
        }
        fprintf(fpvcd,"$version Generated by thumbulator $end\n");
        fprintf(fpvcd,"$timescale 1ns $end\n");
        fprintf(fpvcd,"$scope module thumb $end\n");
        for(ra=0;ra<16;ra++)
        {
            fprintf(fpvcd,"$var wire 32 r%u r%u $end\n",ra,ra);
        }
        fprintf(fpvcd,"$var wire 16 inst inst $end\n");
        fprintf(fpvcd,"$upscope $end\n");
        fprintf(fpvcd,"$enddefinitions $end\n");
        vcdcount=0;
        fprintf(fpvcd,"#%u\n",vcdcount++);
        for(ra=0;ra<16;ra++)
        {
            fprintf(fpvcd,"b0 r%u\n",ra);
        }
        fprintf(fpvcd,"b0 inst\n");
    }

    memset(ram,0x00,sizeof(ram));
    
    // All emulator initialization is done.   Setup the SVC layer.  
    svc_init();
    
    run();
    
    if(output_vcd)
    {
        fclose(fpvcd);
    }
    return(0);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------



//-------------------------------------------------------------------------
//
// Copyright (c) 2010 David Welch dwelch@dwelch.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------



