#ifndef PYC64_H
#define PYC64_H
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bounded arenas; no per-expression allocation. Index zero is the null node. */
#define MAX_SOURCE 65536
#define MAX_TOKENS 8192
#define MAX_NODES 4096
#define MAX_SYMBOLS 512
#define MAX_FUNCTIONS 128
#define MAX_CLASSES 32
#define MAX_FIELDS 32
#define TY_CLASS_BASE 100
#define TY_ENUM_BASE 200
#define MAX_ENUMS 32
#define MAX_ENUM_MEMBERS 256
#define TY_NAMED_BASE 1000
#define MAX_IMPORTS 128
#define MAX_MODULES 32
#define MAX_PATH 1024
#define MAX_SEARCH_PATHS 16
#define MAX_STRINGS 8192
#define MAX_BYTES 32768
#define MAX_ASSETS 64
#define MAX_ASSET_PAYLOAD (MAX_BYTES - 4)
#define MAX_LABELS 8192
#define MAX_FIXUPS 16384
#define MAX_DEPTH 64
#define VERSION "0.1.0"

enum TokenKind {
  T_EOF = 256,
  T_NEWLINE,
  T_INDENT,
  T_DEDENT,
  T_NAME,
  T_NUMBER,
  T_STRING,
  T_EQ,
  T_NE,
  T_LE,
  T_GE,
  T_SHL,
  T_SHR,
  T_ARROW,
  T_ADDSET,
  T_SUBSET,
  T_MULSET,
  T_ANDSET,
  T_ORSET,
  T_XORSET,
  T_FLOORDIV,
  T_FLOAT,
  T_POWER,
  T_POWERSET,
  T_DIVSET,
  T_FLOORSET,
  T_MODSET,
  T_BYTES
};
enum Type {
  TY_NONE,
  TY_BOOL,
  TY_U8,
  TY_I8,
  TY_U16,
  TY_I16,
  TY_STR,
  TY_ARRAY,
  TY_VOID,
  TY_LIST,
  TY_FLOAT,
  TY_BYTES
};
enum Kind {
  N_CONST = 1,
  N_STRING,
  N_NAME,
  N_ATTR,
  N_INDEX,
  N_CALL,
  N_BINARY,
  N_UNARY,
  N_ASSIGN,
  N_IF,
  N_WHILE,
  N_FOR,
  N_FUNC,
  N_RETURN,
  N_EXPR,
  N_BREAK,
  N_CONTINUE,
  N_PASS,
  N_HW,
  N_PARAM,
  N_DECLCONST,
  N_LIST,
  N_EACH,
  N_ASSERT,
  N_CLASS,
  N_FIELD,
  N_CONSTRUCT,
  N_PROPERTY_CALL,
  N_FLOAT,
  N_FLOAT_CAST,
  N_ENUMDEF,
  N_ENUM_LOOKUP,
  N_ENUM_VALUE,
  N_BYTES,
  N_SLICE,
  N_NONE,
  N_BYTES_SEARCH
};
enum Op { OP_AND = 512, OP_OR, OP_NOT, OP_IN, OP_NOTIN };
enum ByteSearch {
  BS_FIND = 1,
  BS_RFIND,
  BS_INDEX,
  BS_RINDEX,
  BS_COUNT,
  BS_PREFIX,
  BS_SUFFIX
};
enum Builtin {
  BI_PRINT = 1,
  BI_CLEAR,
  BI_PUTCHAR,
  BI_PEEK,
  BI_POKE,
  BI_LEN,
  BI_ARRAY,
  BI_KEY_READ,
  BI_KEY_CLEAR,
  BI_SPRITE_ENABLE,
  BI_SPRITE_DISABLE,
  BI_SPRITE_POSITION,
  BI_SPRITE_COLOR,
  BI_SPRITE_DATA,
  BI_FILE_OPEN,
  BI_FILE_CLOSE,
  BI_FILE_READ,
  BI_FILE_WRITE,
  BI_FILE_TEXT,
  BI_FILE_STATUS,
  BI_FILE_ERROR,
  BI_ORD,
  BI_CHR,
  BI_INPUT,
  BI_INT,
  BI_POW,
  BI_MATH_POW,
  BI_MATH_SQRT,
  BI_MATH_EXP,
  BI_MATH_LOG,
  BI_MATH_SIN,
  BI_MATH_COS,
  BI_MATH_TAN,
  BI_MATH_ATAN,
  BI_MATH_FABS,
  BI_MATH_FLOOR,
  BI_MATH_CEIL,
  BI_MATH_TRUNC,
  BI_BYTES,
  BI_REU_PRESENT,
  BI_REU_TRANSFER,
  BI_IRQ_INSTALL,
  BI_IRQ_LINE,
  BI_IRQ_OFF,
  BI_IRQ_ACTIVE,
  BI_IRQ_DISABLE,
  BI_IRQ_RESTORE,
  BI_ASSET_ADDRESS,
  BI_VIDEO_MODE,
  BI_MUSIC_START,
  BI_MUSIC_STOP,
  BI_MUSIC_ACTIVE,
  BI_MUSIC_TICKS,
  BI_CAST_BASE = 64
};
enum AssetMode {
  AS_BINARY = 1,
  AS_SPRITE,
  AS_MULTICOLOR,
  AS_CHARSET,
  AS_TILESET,
  AS_TILEMAP,
  AS_BITMAP,
  AS_BITMAP_SCREEN,
  AS_MUSIC
};
typedef struct {
  int kind, start, length, line, column, module;
  long value;
  double real;
} Token;
typedef struct {
  int kind, op, type, token, a, b, c, d, next, symbol, else_body, accessor;
  long value;
  double real;
} Node;
typedef struct {
  char name[128];
  int type, element, count, scope, label, module, constant, initialized;
  int reference, iterating, parent;
  long value;
  double real;
} Symbol;
typedef struct {
  char name[192];
  int node, label, param[16], argc, type, state, used, module, irq;
} Function;
typedef struct {
  char name[64];
  int node, module, fields[MAX_FIELDS], offsets[MAX_FIELDS], nfields, size;
  int getters[MAX_FIELDS], setters[MAX_FIELDS], nproperties;
} ClassLayout;

typedef struct {
  char name[64];
  int token, canonical, string;
  long value;
} EnumMember;
typedef struct {
  char name[64];
  int node, module, type, first, count, ready;
} EnumLayout;
typedef struct {
  char alias[64], target[192], name[96], member[64], prefix[96];
  int module, destination, token, builtin;
} Import;
typedef struct {
  char path[MAX_PATH], name[96];
  int start, length, first_token, root, tail, state, import_token;
} Module;
typedef struct {
  char path[MAX_PATH], palette[112];
  int mode, offset, count;
} Asset;
typedef struct {
  int load, init, play, songs, start, offset, length, flags, version, rsid;
  uint32_t speed;
} SidInfo;
int sid_info(const unsigned char *data, int count, SidInfo *info, char *error,
             size_t size);
int rsid_info(const unsigned char *data, int count, SidInfo *info, char *error,
              size_t size);
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable                                                        \
                : 4324) /* Host arenas intentionally use native alignment. */
#endif
typedef struct {
  const char *path;
  char source[MAX_SOURCE];
  int source_length;
  Token tokens[MAX_TOKENS];
  int ntokens, cursor;
  Node nodes[MAX_NODES];
  int nnodes, root;
  Symbol symbols[MAX_SYMBOLS];
  int nsymbols;
  Function functions[MAX_FUNCTIONS];
  int nfunctions;
  ClassLayout classes[MAX_CLASSES];
  int nclasses;
  EnumLayout enums[MAX_ENUMS];
  EnumMember enum_members[MAX_ENUM_MEMBERS];
  int nenums, nenum_members;
  Import imports[MAX_IMPORTS];
  int nimports;
  Module modules[MAX_MODULES];
  int nmodules, active_module;
  char search_paths[MAX_SEARCH_PATHS][MAX_PATH];
  int nsearch_paths;
  char strings[MAX_STRINGS];
  int nstrings;
  unsigned char bytes[MAX_BYTES];
  int nbytes;
  unsigned short byte_alignment[MAX_BYTES];
  unsigned char byte_vic[MAX_BYTES];
  Asset assets[MAX_ASSETS];
  int nassets, asset_size;
  char asset_root[MAX_PATH];
  int target, scope, loop_depth, depth, constant_context, parsing_class;
  unsigned char image[65536];
  int image_size, code_size, data_size;
  FILE *listing, *map;
  jmp_buf abort;
} Compiler;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
void fail(Compiler *c, int token, const char *format, ...);
void token_text(Compiler *c, int token, char *out, size_t size);
void lex(Compiler *c);
void parse(Compiler *c);
void load_program(Compiler *c, const char *path);
void resolve_name(Compiler *c, int node, int *module, char *name, size_t size);
void resolve_export(Compiler *c, Import *im, int *module, char *name,
                    size_t size);
void check_binding(Compiler *c, int token);
int library_type(Compiler *c, int node);
int module_namespace(Compiler *c, int node);
int named_expression(Compiler *c, int node);
int object_type(int type);
int class_named(Compiler *c, int module, const char *name);
int class_field(Compiler *c, int type, int token);
int class_property(Compiler *c, int type, int token, int writing);
void function_name(Compiler *c, int node, char *name, size_t size);
void prepare_classes(Compiler *c);
void prepare_enums(Compiler *c);
int parse_bytes(Compiler *c, int token);
void bytes_literal(Compiler *c, int node, const unsigned char *data, int count);
int asset_kind(const char *name);
void asset_load(Compiler *c, int node, int mode);
int platform_asset(const char *path, int mode, const char *palette,
                   unsigned char *data, int capacity, char *error, size_t size);
void bytes_slice_literal(Compiler *c, int node);
int bytes_method(const char *name);
int bytes_method_type(int method);
void bytes_search_literal(Compiler *c, int node);
int enum_type(int type);
int integer_type(int type);
int enum_named(Compiler *c, int module, const char *name);
int enum_class(Compiler *c, int node);
int enum_value_member(Compiler *c, int type, long value);
int enum_string(Compiler *c, int member);
void enum_constant(Compiler *c, int node, int type, int member, int name);
void analyze(Compiler *c);
void infer_signatures(Compiler *c);
void validate_ir(Compiler *c);
int irq_handler(Compiler *c, int reference);
int irq_registration(Compiler *c, int node);
void validate_irq(Compiler *c);
int symbol_size(const Symbol *s);
void optimize(Compiler *c);
void fold_expression(Compiler *c, int node);
void generate(Compiler *c);
int compile_source(Compiler *c, const char *command, const char *source,
                   const char *output, const char *map);
int type_size(int type);
int scalar_type(int type);
int power_type(Compiler *c, int base, int exponent, int exponent_node);
void float_pack(Compiler *c, int token, double value, unsigned char *bytes);
unsigned int float_integer(Compiler *c, int token, double value, int bounded);
int type_signed(int type);
const char *type_name(int type);
int node_new(Compiler *c, int kind, int token);
int platform_read(Compiler *c, const char *path);
int platform_exists(const char *path);
int platform_write(const char *path, const unsigned char *data, size_t size);
int platform_same_file(const char *a, const char *b);
#endif
