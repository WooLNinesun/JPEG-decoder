#include <stdio.h>

#include "jpegDecoder.h"

using namespace std;

jpegDecoder::jpegDecoder( const char* filepath ){
    if ( !jpeg_open( filepath ) ) {
        throw "File open error or isn't a JPEG file.";
    }

    while ( !(this->hasEOI) ) {
        switch( read_byte() ) {
            case 0xFF: {
                if ( read_ctrl() ) { break; }
            }
            default: {
                throw "File format error.";
            }
        }
    }
}

jpegDecoder::~jpegDecoder() {
    unsigned char ht_info[4] = { 0x00, 0x01, 0x10, 0x11 };
    for ( unsigned char i = 0; i < 4; i++ ) {
        huffmanTable_el* ht = this->huffmancode_tables.get( ht_info[i] );
        for ( unsigned char j = 0; j < 16; j++ ) {
            if ( ht[j].num == 0 ) { continue; }
            delete [] ht[j].symbol;
            delete [] ht[j].codeword;
        }
    }
}

bool jpegDecoder::read_ctrl() {
    unsigned char marker = read_byte();

    switch( marker ) {
        // SOF0 = Start Of Frame (baseline DCT)
        case 0xC0: { return this->read_header_SOF(); }
        // DHT = Define Huffman Tables
        case 0xC4: { return this->read_header_DHT(); }
        // SOI = start of image
        case 0xD8: { return true; }
        // EOI = end of image
        case 0xD9: { this->hasEOI = true; return true; }
        // SOS = Start Of Scan
        case 0xDA: { return this->read_header_SOS() && this->read_data(); }
        // DQT = Define Quantization Tables
        case 0xDB: { return this->read_header_DQT(); }
        // DNL = Define Number of Lines. skip it
        case 0xDC: { return this->read_header_skip( marker, "DNL" ); }
        // DRI = Define Restart Interval. skip it
        case 0xDD: { return this->read_header_DRI(); }
        // DHP = Define Hierarchical Progression
        case 0xDE: { return this->read_header_skip( marker, "DHP" ); }
        // EXP = Expand Reference Components
        case 0xDF: { return this->read_header_skip( marker, "EXP" ); }
        // APPn = Application specific segments
        case 0xE0: case 0xE1: case 0xE2: case 0xE3:
        case 0xE4: case 0xE5: case 0xE6: case 0xE7:
        case 0xE8: case 0xE9: case 0xEA: case 0xEB:
        case 0xEC: case 0xED: case 0xEE: case 0xEF: {
            return this->read_header_skip( marker, "APP" );
        }
        // COM = Comment
        case 0xFE: { return this->read_header_skip( marker, "COM" ); }
        default:{ return false; }
    }
}

bool jpegDecoder::read_data() {
    int w = (this->img.width - 1)  / (8 * this->Hmax) + 1;
    int h = (this->img.height - 1) / (8 * this->Vmax) + 1;
    for ( int i = 0; i < h; i++ ) {
        for ( int j = 0; j < w; j++ ) {
            // printf("MCU (%d,%d)\n", i, j);
            read_MCU();
        }
    } return true;
}

bool jpegDecoder::read_MCU() {
    for ( unsigned char i = 0; i < this->components_num; i++ ) {
        component* cpt = &(this->components[i]);
        for ( unsigned char h = 0; h < cpt->hori; h++ ) {
            for ( unsigned char w = 0; w < cpt->vert; w++ ) {
                // printf("\tDataUnit: (%d,%d,%d)\n", i, h, w);
                // printf("\t\tDC Predictor: %d\n\t\tDC:\n",
                //     this->components[i].DC_predictor);
                el_code DC = this->read_DC(
                    cpt->ht_DC, &this->components[i].DC_predictor );

                // printf("\t\tAC:\n");
                for( int count = 0; count < 63; ) {
                    el_code AC = read_AC( cpt->ht_AC );
                    if (AC.size == 0 && AC.zeros == 0) { break; }
                    for (int j = 0; j < AC.zeros; j++) {
                        count++;
                    } count++;
                }
            }
        }
    }
}

unsigned char jpegDecoder::match_huffman_tables( unsigned char ht_info ) {
    huffmanTable_el* ht = this->huffmancode_tables.get(ht_info);
    if ( ht == 0 ) { throw "huffman_table get error"; }

    unsigned short codeword = 0;
    for ( int i = 0; i < 16; i++ ) {
        codeword <<= 1; codeword += (unsigned short)this->read_bit();
        for ( int j = 0; j < ht[i].num; j++ ) {
            if ( ht[i].codeword[j] == codeword ) {
                return ht[i].symbol[j];
            }
        }
    } throw "key not found.";
}

el_code jpegDecoder::read_DC( unsigned char ht_DC, int* predictor ) {
    unsigned char ht_info = 0x00 + (ht_DC);
    unsigned char codelen = this->match_huffman_tables( ht_info );
    
    int ret = 0;
    if ( codelen != 0 ) {
        bool first = read_bit(); ret = 1;
        for ( int i = 1; i < codelen; i++ ) {
            bool b = read_bit();
            ret <<= 1; ret += first ? b : !b;
        } ret = (first)? ret : -ret;
    } *predictor += ret;

    // printf("\t\t\tT: %d\tDIFF: 0\tEXTEND(DIFF,T): %d\n", codelen, ret);

    return el_code { 0, codelen, ret };
}

el_code jpegDecoder::read_AC( unsigned char ht_AC ) {
    unsigned char ht_info = 0x10 + (ht_AC);
    unsigned char zeros_codelen = this->match_huffman_tables( ht_info );

    unsigned char zeros   = zeros_codelen >> 4;
    unsigned char codelen = zeros_codelen & 0x0F;
    
    int ret = 0;
    if ( codelen != 0 ) {
        bool first = read_bit(); ret = 1;
        for ( int i = 1; i < codelen; i++ ) {
            bool b = read_bit();
            ret <<= 1; ret += first ? b : !b;
        } ret = (first)? ret : -ret;
    } else if ( zeros != 0 && zeros != 0x0F ) {
        throw "Read AC code error.";
    }

    // if ( codelen == 0 && zeros == 0) {
    //     printf("\t\t\tRS: 0x00 -- EOB\n");
    // } else {
    //     printf( "\t\t\tRS: %.2x RR:%4d SS:%4d",
    //         zeros_codelen, zeros, codelen);
    //     if ( codelen != 0 ) {
    //         printf( " ZZ(K):%2d EXTEND(ZZ(K),SS):%4d", 0, ret);
    //     } printf("\n");
    // }

    return el_code { zeros, codelen, ret };
}

        }
    }
    }
}

}

        }
    }
