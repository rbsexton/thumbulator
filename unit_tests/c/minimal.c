
//-------------------------------------------------------------------
// Copyright (C) 2020 Robert Sexton
//-------------------------------------------------------------------

//*****************************************************************************
// The simplest possible program.
//*****************************************************************************
#include <stdbool.h> 
#include <stdint.h> 

volatile uint32_t counter;

//
int main() {
  for ( int i = 0; i < 9 ; i++ ) {
    counter = counter + 1; 
  }

  __asm("bkpt 0\r");
  
}

