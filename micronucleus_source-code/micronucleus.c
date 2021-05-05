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

  Modified in 2021 by Franzininho
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
        printf("\nO tipo de arquivo especificado em --type option é desconhecido.");
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[arg_pointer], "--help") == 0 || strcmp(argv[arg_pointer], "-h") == 0) {
      puts(usage);
      printf("\n");
      puts("  --type [intel-hex, raw]: Define o arquivo de upload como intel-hex ou raw");
      puts("                           (intel-hex é padrão).");
      puts("          --dump-progress: Define uma forma mais amigável de apresentação dos dados");
      puts("                           de saída do programa para interface gráfica.");
      puts("            --erase-only:  Apaga a memória do dispositivo sem carregar um novo programa.");
      puts("                           Preenche a memória de programa (Flash memory) com 0xFFFF.");
      puts("                           Qualquer outro arquivo é ignorado.");
      puts("              --fast-mode: Acelera o tempo de conexão entre o programa e o dispositivo em 2ms.");
      puts("                           Não use se ocorrer erros na conexão USB!");
      puts("                    --run: Invoca o bootloader para iniciar o programa quando");
      puts("                           o upload terminar.");
      #ifndef WIN
      puts("                --no-ansi: Desabilita o uso de ANSI na saída do terminal.");
      #endif
      puts("      --timeout [integer]: Define um tempo de espera especificado em segundos.");
      puts("                 filename: Define um caminho para o arquivo a ser feito o upload: intel-hex ou raw.");
      puts("                           Ou \"-\" para ser lido por stdin.");
      printf("\n");
      printf("Versão da ferramenta de upload: %s.", MICRONUCLEUS_COMMANDLINE_VERSION);
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
        printf("\nValor inválido de --timeout.");
        return EXIT_FAILURE;
      }
    } else {
      file = argv[arg_pointer];
    }

    arg_pointer += 1;
  }  

  if (file == NULL && erase_only == 0) {
    printf("\nNão foi fornecido um arquivo para upload nem o comando --erase-only!\n\n");
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

  printf("> Versão do dispositivo: %d.%d\n", my_device->version.major, my_device->version.minor);
  if (my_device->signature1) printf("> Assinatura do dispositivo: 0x1e%02x%02x\n", (int)my_device->signature1, (int)my_device->signature2);
  printf("> Espaço disponível para aplicações: %d bytes\n", my_device->flash_size);
  printf("> Tempo de sleep sugerido entre o envio das pages: %ums\n", my_device->write_sleep);
  printf("> Quantidade total de pages: %d - Tamanho da page: %d\n", my_device->pages,my_device->page_size);
  printf("> Tempo de sleep da função EraseFlash: %dms\n", my_device->erase_sleep);
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
      printf("> O arquivo de programa excede em %d bytes o espaço disponível!\n", endAddress - my_device->flash_size);
      return EXIT_FAILURE;
    }
  }

  printProgress(1.0);

  setProgressData("apagando", 4);
  printf("> Apagando a memória ...\n");
  fflush(stdout);
  res = micronucleus_eraseFlash(my_device, printProgress);

  if (res == 1) { // erase disconnection bug workaround
    printf(">> Ops! A conexão com o dispositivo foi perdida enquanto a memória era apagada! Não se preocupe\n");
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
        printf(">> (!) Não foi possível reestabelecer a conexão. Desconecte e reconecte\n");
        printf("   o dispositivo, ou pressione o botão de reset.\n");
        fflush(stdout);
      }
    }

    printf(">> Conexão reestabelecida! Continuando a sequência de upload...\n");
    fflush(stdout);

  } else if (res != 0) {
    printf(">> Erro %d ocorreu enquanto a memória Flash era apagada...\n", res);
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
      printf(">> Erro %d ocorreu enquanto a memória Flash era escrita...\n", res);
      printf(">> Por favor desconecte o dispositivo e reinicie o programa.\n");
      return EXIT_FAILURE;
    }
  }

  if (run) {
    printf("> Iniciando a aplicação do usuário ...\n");
    fflush(stdout);
    setProgressData("iniciando", 6);
    printProgress(0.0);

    res = micronucleus_startApp(my_device);

    if (res != 0) {
      printf(">> Erro %d ocorreu enquanto aplicação do usuário era iniciada...\n", res);
      printf(">> Por favor desconecte o dispositivo e reinicie o programa.\n");
      return EXIT_FAILURE;
    }

    printProgress(1.0);
  }

  printf(">> Upload pelo Micronucleus concluído. Obrigado!\n");
  fflush(stdout);

  return EXIT_SUCCESS;
}
/******************************************************************************/

/******************************************************************************/
static void printProgress(float progress) {
  static int last_step;
  static int last_integer_total_progress;

  if (dump_progress) {
    printf("{status:\"%s\",estágio atual:%d,total de estágios:%d,progresso:%f}\n", progress_friendly_name, progress_step, progress_total_steps, progress);
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
      printf("> Aviso: Erro no checksum entre os endereços 0x%x e 0x%x\n", base, address);
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
