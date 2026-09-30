#ifndef MAKEIDM_H
#define MAKEIDM_H

#include <stdint.h>

#pragma pack(push, 1) // Garante que o compilador não adicione preenchimento (padding) oculto nos bytes

// 1. ASSINATURA MÁGICA DO FORMATO
#define IDM_MAGIC 0x4D444900 // Bytes "IDM\x00" em formato Little Endian

// 2. IDENTIFICADORES DE TIPO DE RECURSO
#define RSRC_TYPE_TEXT   0x01 // Texto simples / Strings da interface
#define RSRC_TYPE_BINARY 0x02 // Injeção binária pura (Áudio, texturas, substituições de .exe)

// 3. CABEÇALHO PRINCIPAL DO ARQUIVO .IDM (Global Header)
typedef struct {
    uint32_t magic;          // Sempre "IDM\x00" para validar que o arquivo é legítimo
    uint32_t version;        // Versão do formato .idm (ex: 1)
    uint32_t total_entries;  // Quantidade total de blocos/recursos salvos neste arquivo
    uint32_t checksum_exe;   // Checksum do executável original para evitar mismatch
    uint8_t  reserved[16];   // Espaço reservado para expansões futuras (preenchido com 0)
} IdmHeader;

// 4. ENTRADA DA TABELA DE ÍNDICES (Index Table Entry)
// O configidm lê esta tabela sequencialmente para fazer o "Strip" ultra rápido
typedef struct {
    uint32_t resource_id;    // Hash numérico ou ID único gerado a partir do nome (ex: MSG_WELCOME)
    uint16_t language_id;    // ID do idioma em formato numérico (ex: 0x5450 para "PT", 0x5345 para "ES")
    uint8_t  resource_type;  // RSRC_TYPE_TEXT ou RSRC_TYPE_BINARY
    uint64_t target_offset;  // Endereço no .exe para aplicação (usado se for RSRC_TYPE_BINARY, senão 0)
    uint32_t data_size;      // Tamanho físico em bytes do payload que vem a seguir
    uint64_t data_offset;    // Posição absoluta em bytes (posição no arquivo) onde os dados começam
} IdmIndexEntry;

#pragma pack(pop)

#endif // MAKEIDM_H
