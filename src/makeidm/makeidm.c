#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "makeidm.h"

/* Simple Hash function (DJB2) to convert resource IDs like "MSG_WELCOME" into a 32-bit integer */
uint32_t generate_hash_id(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/* Helper function to convert a 2-character language string (e.g., "PT") into a 16-bit integer ID */
uint16_t convert_lang_id(const char *lang_str) {
    if (strlen(lang_str) < 2) return 0;
    return (uint16_t)((lang_str[1] << 8) | lang_str[0]);
}

/* Converts a hex string (e.g., "415544") into raw binary bytes */
uint8_t* hex_to_bytes(const char *hex_str, uint32_t *out_size) {
    uint32_t len = strlen(hex_str);
    if (len % 2 != 0) return NULL; /* Invalid hex string (must be an even length) */
    
    *out_size = len / 2;
    uint8_t *buffer = (uint8_t*)malloc(*out_size);
    if (!buffer) return NULL;
    
    for (uint32_t i = 0; i < *out_size; i++) {
        unsigned int byte_val;
        sscanf(&hex_str[i * 2], "%2x", &byte_val);
        buffer[i] = (uint8_t)byte_val;
    }
    return buffer;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <input.translate> <output.idm>\n", argv[0]);
        return 1;
    }

    FILE *txt_in = fopen(argv[1], "r");
    if (!txt_in) {
        printf("Error: Could not open input file: %s\n", argv[1]);
        return 1;
    }

    /* Fixed-size structures for demonstration. Real-world implementations should use dynamic arrays. */
    IdmIndexEntry index_table[1024];
    uint8_t* payloads[1024];
    uint32_t total_entries = 0;

    char line[4096];
    char current_id[256] = {0};
    uint64_t current_offset = 0;

    printf("Reading and compiling %s...\n", argv[1]);

    /* Parser loop for the .translate file */
    while (fgets(line, sizeof(line), txt_in)) {
        /* Strip line breaks */
        line[strcspn(line, "\r\n")] = 0;

        /* If original resource tag is found, store global metadata for this block */
        if (strstr(line, "<RESOURCE_ORIGINAL>")) {
            current_id[0] = '\0';
            current_offset = 0;
            
            while (fgets(line, sizeof(line), txt_in) && !strstr(line, "</RESOURCE_ORIGINAL>")) {
                if (strstr(line, "ID:")) {
                    sscanf(line, "    ID: %255s", current_id);
                } else if (strstr(line, "OFFSET:")) {
                    sscanf(line, "    OFFSET: %lx", &current_offset);
                }
            }
        }
        
        /* If TEXT translation tag is found (e.g., <RESOURCE_TRANSLATED:PT>) */
        char *text_tag = strstr(line, "<RESOURCE_TRANSLATED:");
        if (text_tag && current_id[0] != '\0') {
            char lang_str[3] = {0};
            sscanf(text_tag, "<RESOURCE_TRANSLATED:%2s>", lang_str);
            
            while (fgets(line, sizeof(line), txt_in) && !strstr(line, "</RESOURCE_TRANSLATED:")) {
                if (strstr(line, "TEXT:")) {
                    char raw_text[2048] = {0};
                    char *start_quote = strchr(line, '"');
                    char *end_quote = start_quote ? strrchr(start_quote + 1, '"') : NULL;
                    
                    if (start_quote && end_quote) {
                        size_t text_length = end_quote - (start_quote + 1);
                        strncpy(raw_text, start_quote + 1, text_length);
                        raw_text[text_length] = '\0';

                        IdmIndexEntry entry;
                        entry.resource_id = generate_hash_id(current_id);
                        entry.language_id = convert_lang_id(lang_str);
                        entry.resource_type = RSRC_TYPE_TEXT;
                        entry.target_offset = 0;
                        entry.data_size = (uint32_t)(text_length + 1); /* +1 for null-terminator */
                        entry.data_offset = 0; /* Calculated before writing */

                        index_table[total_entries] = entry;
                        payloads[total_entries] = (uint8_t*)strdup(raw_text);
                        total_entries++;
                    }
                }
            }
        }

        /* If BINARY substitution tag is found (e.g., <SUBSTITUTER_RSRC:PT>) */
        char *binary_tag = strstr(line, "<SUBSTITUTER_RSRC:");
        if (binary_tag && current_id[0] != '\0') {
            char lang_str[3] = {0};
            sscanf(binary_tag, "<SUBSTITUTER_RSRC:%2s>", lang_str);
            
            while (fgets(line, sizeof(line), txt_in) && !strstr(line, "</SUBSTITUTER_RSRC>")) {
                if (strstr(line, "BINARY:")) {
                    char hex_str[4096] = {0};
                    sscanf(line, "    BINARY: %4095s", hex_str);
                    
                    uint32_t out_size = 0;
                    uint8_t *bytes = hex_to_bytes(hex_str, &out_size);
                    
                    if (bytes) {
                        IdmIndexEntry entry;
                        entry.resource_id = generate_hash_id(current_id);
                        entry.language_id = convert_lang_id(lang_str);
                        entry.resource_type = RSRC_TYPE_BINARY;
                        entry.target_offset = current_offset;
                        entry.data_size = out_size;
                        entry.data_offset = 0; /* Calculated before writing */

                        index_table[total_entries] = entry;
                        payloads[total_entries] = bytes;
                        total_entries++;
                    }
                }
            }
        }
    }
    fclose(txt_in);

    /* =======================================================================
       BINARY GENERATION (.IDM FILE)
       ======================================================================= */
    FILE *bin_out = fopen(argv[2], "wb");
    if (!bin_out) {
        printf("Error: Could not create output binary file.\n");
        return 1;
    }

    /* 1. Setup and write Global Header */
    IdmHeader header;
    header.magic = IDM_MAGIC;
    header.version = 1;
    header.total_entries = total_entries;
    header.checksum_exe = 0xABCDEF12; /* Example target executable checksum */
    header.reserved = 0;
    fwrite(&header, sizeof(IdmHeader), 1, bin_out);

    /* 2. Calculate data offsets (Payload positions) */
    uint64_t current_payload_offset = sizeof(IdmHeader) + (sizeof(IdmIndexEntry) * total_entries);
    for (uint32_t i = 0; i < total_entries; i++) {
        index_table[i].data_offset = current_payload_offset;
        current_payload_offset += index_table[i].data_size;
    }

    /* 3. Write Index Table block */
    fwrite(index_table, sizeof(IdmIndexEntry), total_entries, bin_out);

    /* 4. Write data Payloads */
    for (uint32_t i = 0; i < total_entries; i++) {
        fwrite(payloads[i], 1, index_table[i].data_size, bin_out);
        free(payloads[i]);
    }

    fclose(bin_out);
    printf("Success! Binary file '%s' generated with %d entries.\n", argv[2], total_entries);
    return 0;
}
