/*
Created: September 2012
  (c) 2012 by ihsan Kehribar <ihsan@kehribar.me>
  Changes for Micronucleus protocol version V2.x
  (c) 2014 T. Bo"scke

  Permission is hereby granted, free of charge, to any person obtaining a copy of
  this software and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the rights to
  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
  of the Software, and to permit persons to whom the Software is furnished to do
  so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "micronucleus_lib.h"
#include "littleWire_util.h"

#define FILE_TYPE_INTEL_HEX 1
#define FILE_TYPE_RAW 2
#define CONNECT_WAIT 250 /* milliseconds to wait after detecting device on usb bus - probably excessive */

#define a_acentoagudo 160
#define e_acendoagudo 130
#define i_acentoagudo 161
#define o_acentoagudo 162
#define a_til 198
#define o_til 228
#define e_circunflexo 136
#define cedilha 135

/******************************************************************************
* Global definitions
******************************************************************************/
unsigned char dataBuffer[65536 + 256];    /* buffer for file data */
/*****************************************************************************/

/******************************************************************************
* Function prototypes
******************************************************************************/
static int parseRaw(char *hexfile, unsigned char *buffer, int *startAddr, int *endAddr);
static int parseIntelHex(char *hexfile, unsigned char *buffer, int *startAddr, int *endAddr); /* taken from bootloadHID example from obdev */
static int parseUntilColon(FILE *fp); /* taken from bootloadHID example from obdev */
static int parseHex(FILE *fp, int numDigits); /* taken from bootloadHID example from obdev */
static void printProgress(float progress);
static void setProgressData(char* friendly, int step);
static int progress_step = 0; // current step
static int progress_total_steps = 0; // total steps for upload
static char* progress_friendly_name; // name of progress section
static int dump_progress = 0; // output computer friendly progress info
static int use_ansi = 0; // output ansi control character stuff
static int erase_only = 0; // only erase, dont't write file
static int fast_mode = 0; // normal mode adds 2ms to page writing times and waits longer for connect.
static int timeout = 0;
/*****************************************************************************/

/******************************************************************************
* Main function!
******************************************************************************/
int main(int argc, char **argv) {
  int res;
  char *file = NULL;
  micronucleus *my_device = NULL;
  
  // parse arguments
  int run = 0;
  int file_type = FILE_TYPE_INTEL_HEX;
  int arg_pointer = 1;
  #if defined(WIN)
  char* usage = "usage: micronucleus [--help] [--run] [--dump-progress] [--fast-mode] [--type intel-hex|raw] [--timeout integer] (--erase-only | filename)";
  #else
  char* usage = "usage: micronucleus [--help] [--run] [--dump-progress] [--fast-mode] [--type intel-hex|raw] [--timeout integer] [--no-ansi] (--erase-only | filename)";
  #endif
  progress_step = 0;
  progress_total_steps = 5; // steps: waiting, connecting, parsing, erasing, writing, (running)?
  dump_progress = 0;
  erase_only = 0;
  fast_mode=0;
  timeout = 0; // no timeout by default
  #if defined(WIN)
    use_ansi = 0;
  #else
    use_ansi = 1;
  #endif

  while (arg_pointer < argc) {
    if (strcmp(argv[arg_pointer], "--run") == 0) {
      run = 1;
      progress_total_steps += 1;
    } else if (strcmp(argv[arg_pointer], "--type") == 0) {
      arg_pointer += 1;
      if (strcmp(argv[arg_pointer], "intel-hex") == 0) {
        file_type = FILE_TYPE_INTEL_HEX;
      } else if (strcmp(argv[arg_pointer], "raw") == 0) {
        file_type = FILE_TYPE_RAW;
      } else {
        printf("\nO tipo de arquivo especificado em --type option %c desconhecido", e_acendoagudo);
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[arg_pointer], "--help") == 0 || strcmp(argv[arg_pointer], "-h") == 0) {
      puts(usage);
      printf("\n  --type [intel-hex, raw]: Define o arquivo de upload como intel-hex ou raw");
      printf("\n                           (intel-hex %c padr%co)", e_acendoagudo, a_til);
      printf("\n          --dump-progress: Define uma forma mais amig%cvel de apresenta%c%co dos dados", a_acentoagudo, cedilha, a_til);
      printf("\n                           de sa%cda do programa para interface gr%cfica", i_acentoagudo, a_acentoagudo);
      printf("\n            --erase-only:  Apaga a mem%cria do dispositivo, sem carregar um novo programa.", o_acentoagudo);
      printf("\n                           Preenche a mem%cria de programa (Flash memory) com 0xFFFF.", o_acentoagudo);
      printf("\n                           Qualquer outro arquivo %c ignorado.", e_acendoagudo);
      printf("\n              --fast-mode: Acelera o tempo de conex%co entre o programa e o dispositivo em 2ms.", a_til);
      printf("\n                           N%co use se ocorrer erros na conex%co USB.", a_til, a_til);
      printf("\n                    --run: Invoca o bootloader para iniciar o programa quando");
      printf("\n                           o upload terminar");
      #ifndef WIN
      printf("\n                --no-ansi: Desabilita o uso de ANSI na sa%cda do terminal", i_acentoagudo);
      #endif
      printf("\n      --timeout [integer]: Define um tempo de espera especificado em segundos");
      printf("\n                 filename: Define um caminho para o arquivo a ser feito o upload: intel-hex ou raw.");
      printf("\n                           Ou \"-\" para ser lido por stdin");
      printf("\n");
      printf("Vers%co da ferramenta de upload: %s", a_til, MICRONUCLEUS_COMMANDLINE_VERSION);
      return EXIT_SUCCESS;
    } else if (strcmp(argv[arg_pointer], "--dump-progress") == 0) {
      dump_progress = 1;
    } else if (strcmp(argv[arg_pointer], "--no-ansi") == 0) {
      use_ansi = 0;
    } else if (strcmp(argv[arg_pointer], "--fast-mode") == 0) {
      fast_mode = 1;
    } else if (strcmp(argv[arg_pointer], "--erase-only") == 0) {
      erase_only = 1;
      progress_total_steps -= 1;
    } else if (strcmp(argv[arg_pointer], "--timeout") == 0) {
      arg_pointer += 1;
      if (sscanf(argv[arg_pointer], "%d", &timeout) != 1) {
        printf("\nValor inv%clido de --timeout", a_acentoagudo);
        return EXIT_FAILURE;
      }
    } else {
      file = argv[arg_pointer];
    }

    arg_pointer += 1;
  }  

  if (file == NULL && erase_only == 0) {
    printf("\nN%co foi fornecido um arquivo para upload nem o comando --erase-only!\n\n", a_til);
    puts(usage);
    return EXIT_FAILURE;
  }

  setProgressData("aguardando", 1);
  if (dump_progress) printProgress(0.5);
  printf("> Conecte o dispositivo ou aperte RESET\n");
  //printf("> Pressione CTRL+C para finalizar o programa\n"); //this program will be called by Arduino IDE, so HID input shouldn't work
  fflush(stdout);

  time_t start_time, current_time;
  time(&start_time);

  while (my_device == NULL) {
    delay(100);
    my_device = micronucleus_connect(fast_mode);

    time(&current_time);
    if (timeout && start_time + timeout < current_time) {
      break;
    }
  }

  if (my_device == NULL) {
    printf("> Tempo de procura de dispositivos esgotado!\n");
    return EXIT_FAILURE;
  }

  printf("> Dispositivo encontrado!\n");
  fflush(stdout);

  if (!fast_mode) {
    // wait for CONNECT_WAIT milliseconds with progress output
    float wait = 0.0f;
    setProgressData("conectando", 2);
    while (wait < CONNECT_WAIT) {
      printProgress((wait / ((float) CONNECT_WAIT)) * 0.9f);
      wait += 50.0f;
      delay(50);
    }
  }
  printProgress(1.0);

  printf("> Vers%co do dispositivo: %d.%d\n", a_til, my_device->version.major, my_device->version.minor);
  if (my_device->signature1) printf("> Assinatura do dispositivo: 0x1e%02x%02x\n", (int)my_device->signature1, (int)my_device->signature2);
  printf("> Espa%co dispon%cvel para aplica%c%ces: %d bytes\n", cedilha, i_acentoagudo, cedilha, o_til, my_device->flash_size);
  printf("> Tempo de sleep sugerido entre o envio das pages: %ums\n", my_device->write_sleep);
  printf("> Quantidade total de pages: %d - Tamanho da page: %d\n", my_device->pages,my_device->page_size);
  printf("> Tempo de sleep da fun%c%co EraseFlash: %dms\n", cedilha, a_til, my_device->erase_sleep);
  fflush(stdout);

  int startAddress = 1, endAddress = 0;

  if (!erase_only) {
    setProgressData("analisando", 3);
    printProgress(0.0);
    memset(dataBuffer, 0xFF, sizeof(dataBuffer));

    if (file_type == FILE_TYPE_INTEL_HEX) {
      if (parseIntelHex(file, dataBuffer, &startAddress, &endAddress)) {
        printf("> Erro ao carregar ou analisar hex files.\n");
        return EXIT_FAILURE;
      }
    } else if (file_type == FILE_TYPE_RAW) {
      if (parseRaw(file, dataBuffer, &startAddress, &endAddress)) {
        printf("> Erro ao carregar arquivo raw.\n");
        return EXIT_FAILURE;
      }
    }

    printProgress(1.0);

    if (startAddress >= endAddress) {
      printf("> Sem dados referentes ao arquivo de programa.\n");
      return EXIT_FAILURE;
    }

    if (endAddress > my_device->flash_size) {
      printf("> O arquivo de programa excede em %d bytes o espa%co dispon%cvel!\n", endAddress - my_device->flash_size, cedilha, i_acentoagudo);
      return EXIT_FAILURE;
    }
  }

  printProgress(1.0);

  setProgressData("apagando", 4);
  printf("> Apagando a mem%cria ...\n", o_acentoagudo);
  fflush(stdout);
  res = micronucleus_eraseFlash(my_device, printProgress);

  if (res == 1) { // erase disconnection bug workaround
    printf(">> Ops! A conex%co com o dispositivo foi perdida enquanto a mem%cria era apagada! Não se preocupe\n", a_til, o_acentoagudo);
    printf(">> isso acontece em alguns computadores - reconectando...\n");
    fflush(stdout);
    my_device = NULL;

    delay(CONNECT_WAIT);

    int deciseconds_till_reconnect_notice = 50; // notice after 5 seconds
    while (my_device == NULL) {
      delay(100);
      my_device = micronucleus_connect(fast_mode);
      deciseconds_till_reconnect_notice -= 1;

      if (deciseconds_till_reconnect_notice == 0) {
        printf(">> (!) N%co foi possível reestabelecer a conex%co. Desconecte e reconecte\n",a_til, a_til);
        printf("   o dispositivo, ou pressione o bot%co de reset\n", a_til);
        fflush(stdout);
      }
    }

    printf(">> Conex%co reestabelecida! Continuando a sequ%cncia de upload...\n", a_til, e_circunflexo);
    fflush(stdout);

  } else if (res != 0) {
    printf(">> Erro %d ocorreu enquanto a mem%cria Flash era apagada...\n", res, o_acentoagudo);
    printf(">> Por favor desconecte o dispositivo e reinicie o programa.\n");
    return EXIT_FAILURE;
  }
  printProgress(1.0);

  if (!erase_only) {
    printf("> Iniciando o upload...\n");
    fflush(stdout);
    setProgressData("carregando", 5);
    res = micronucleus_writeFlash(my_device, endAddress, dataBuffer, printProgress);
    if (res != 0) {
      printf(">> Erro %d ocorreu enquanto a mem%cria Flash era escrita...\n", res, o_acentoagudo);
      printf(">> Por favor desconecte o dispositivo e reinicie o programa.\n");
      return EXIT_FAILURE;
    }
  }

  if (run) {
    printf("> Iniciando a aplica%c%co do usuário ...\n", cedilha, a_til);
    fflush(stdout);
    setProgressData("iniciando", 6);
    printProgress(0.0);

    res = micronucleus_startApp(my_device);

    if (res != 0) {
      printf(">> Erro %d ocorreu enquanto aplica%c%co do usu%crio era iniciada...\n", res, cedilha, a_til, a_acentoagudo);
      printf(">> Por favor desconecte o dispositivo e reinicie o programa.\n");
      return EXIT_FAILURE;
    }

    printProgress(1.0);
  }

  printf(">> Upload pelo Micronucleus conclu%cdo. Obrigado!\n", i_acentoagudo);
  fflush(stdout);

  return EXIT_SUCCESS;
}
/******************************************************************************/

/******************************************************************************/
static void printProgress(float progress) {
  static int last_step;
  static int last_integer_total_progress;

  if (dump_progress) {
    printf("{status:\"%s\",est%cgio atual:%d,total de est%cgios:%d,progresso:%f}\n", progress_friendly_name, a_acentoagudo, progress_step, a_acentoagudo, progress_total_steps, progress);
    fflush(stdout);
  } else {
    if (last_step == progress_step && use_ansi) {
      #ifndef WIN
        printf("\033[1F\033[2K"); // move cursor to previous line and erase last update in this progress sequence
      #else
        printf("\r"); // return carriage to start of line so we can type over existing text
      #endif
    }

    float total_progress = ((float) progress_step - 1.0f) / (float) progress_total_steps;
    total_progress += progress / (float) progress_total_steps;
    int integer_total_progress = total_progress * 100.0f;

    if (use_ansi || integer_total_progress >= last_integer_total_progress + 5) {
      printf("%s: %d%% \n", progress_friendly_name, integer_total_progress);
      fflush(stdout);
      last_integer_total_progress = integer_total_progress;      
    }
  }

  last_step = progress_step;
}

static void setProgressData(char* friendly, int step) {
  progress_friendly_name = friendly;
  progress_step = step;
}
/******************************************************************************/

/******************************************************************************/
static int parseUntilColon(FILE *file_pointer) {
  int character;

  do {
    character = getc(file_pointer);
  } while(character != ':' && character != EOF);

  return character;
}
/******************************************************************************/

/******************************************************************************/
static int parseHex(FILE *file_pointer, int num_digits) {
  int iter;
  char temp[9];

  for(iter = 0; iter < num_digits; iter++) {
    temp[iter] = getc(file_pointer);
  }
  temp[iter] = 0;

  return strtol(temp, NULL, 16);
}
/******************************************************************************/

/******************************************************************************/
static int parseIntelHex(char *hexfile, unsigned char *buffer, int *startAddr, int *endAddr) {
  int address, base, d, segment, i, lineLen, sum;
  FILE *input;

  input = strcmp(hexfile, "-") == 0 ? stdin : fopen(hexfile, "r");
  if (input == NULL) {
    printf("> Erro ao abrir %s: %s\n", hexfile, strerror(errno));
    fflush(stdout);
    return 1;
  }

  while (parseUntilColon(input) == ':') {
    sum = 0;
    sum += lineLen = parseHex(input, 2);
    base = address = parseHex(input, 4);
    sum += address >> 8;
    sum += address;
    sum += segment = parseHex(input, 2);  /* segment value? */
    if (segment != 0) {   /* ignore lines where this byte is not 0 */
      continue;
    }

    for (i = 0; i < lineLen; i++) {
      d = parseHex(input, 2);
      buffer[address++] = d;
      sum += d;
    }

    sum += parseHex(input, 2);
    if ((sum & 0xff) != 0) {
      printf("> Aviso: Erro no checksum entre os endere%cos 0x%x e 0x%x\n", cedilha, base, address);
      fflush(stdout);
    }

    if(*startAddr > base) {
      *startAddr = base;
    }
    if(*endAddr < address) {
      *endAddr = address;
    }
  }

  fclose(input);
  return 0;
}
/******************************************************************************/

/******************************************************************************/
static int parseRaw(char *filename, unsigned char *data_buffer, int *start_address, int *end_address) {
  FILE *input;

  input = strcmp(filename, "-") == 0 ? stdin : fopen(filename, "r");

  if (input == NULL) {
    printf("> Erro ao ler %s: %s\n", filename, strerror(errno));
    fflush(stdout);
    return 1;
  }

  *start_address = 0;
  *end_address = 0;

  // read in bytes from file
  int byte = 0;
  while (1) {
    byte = getc(input);
    if (byte == EOF) break;

    *data_buffer = byte;
    data_buffer += 1;
    *end_address += 1;
  }

  fclose(input);
  return 0;
}
/******************************************************************************/
