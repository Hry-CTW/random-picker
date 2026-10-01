#pragma once
// 极简 DEFLATE 解压（零依赖，为了在 mingw 交叉编译时也能读 .xlsx）
// 只实现 inflate：stored / fixed huffman / dynamic huffman
#include <cstdint>
#include <cstring>
#include <vector>

namespace mini {

struct BitReader {
    const uint8_t* p;
    size_t n, pos;
    uint32_t buf;
    int cnt;
    BitReader(const uint8_t* d, size_t len) : p(d), n(len), pos(0), buf(0), cnt(0) {}
    int bit() {
        if (cnt == 0) {
            if (pos >= n) return -1;
            buf = p[pos++];
            cnt = 8;
        }
        int b = buf & 1;
        buf >>= 1;
        cnt--;
        return b;
    }
    int bits(int k) {
        int v = 0;
        for (int i = 0; i < k; i++) {
            int b = bit();
            if (b < 0) return -1;
            v |= b << i;
        }
        return v;
    }
    void align() {
        buf >>= cnt;
        cnt = 0;
    }
};

struct Huff {
    int counts[16];
    std::vector<int> symbols;
    Huff() { memset(counts, 0, sizeof(counts)); }
    void build(const std::vector<uint8_t>& lens) {
        memset(counts, 0, sizeof(counts));
        for (size_t i = 0; i < lens.size(); i++) counts[lens[i]]++;
        counts[0] = 0;
        symbols.assign(lens.size(), 0);
        int offsets[16];
        offsets[0] = 0;
        for (int l = 1; l < 16; l++) offsets[l] = offsets[l - 1] + counts[l - 1];
        for (size_t i = 0; i < lens.size(); i++)
            if (lens[i]) symbols[offsets[lens[i]]++] = (int)i;
    }
    int decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            int b = br.bit();
            if (b < 0) return -1;
            code |= b;
            int count = counts[len];
            if (code - first < count) return symbols[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }
};

static const int LEN_BASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int LEN_EXTRA[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int DIST_BASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int DIST_EXTRA[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const int CL_ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

inline bool inflate(const uint8_t* in, size_t inlen, std::vector<uint8_t>& out) {
    BitReader br(in, inlen);
    bool final = false;
    while (!final) {
        int f = br.bit();
        int t = br.bits(2);
        if (f < 0 || t < 0) return false;
        final = f != 0;
        if (t == 0) {
            br.align();
            if (br.pos + 4 > br.n) return false;
            uint32_t len = br.p[br.pos] | (br.p[br.pos + 1] << 8);
            br.pos += 4;
            if (br.pos + len > br.n) return false;
            out.insert(out.end(), br.p + br.pos, br.p + br.pos + len);
            br.pos += len;
        } else if (t == 1 || t == 2) {
            Huff lit, dist;
            if (t == 1) {
                std::vector<uint8_t> lens(288, 8);
                for (int i = 144; i < 256; i++) lens[i] = 9;
                for (int i = 256; i < 280; i++) lens[i] = 7;
                for (int i = 280; i < 288; i++) lens[i] = 8;
                lit.build(lens);
                dist.build(std::vector<uint8_t>(30, 5));
            } else {
                int hlit = br.bits(5) + 257;
                int hdist = br.bits(5) + 1;
                int hclen = br.bits(4) + 4;
                if (hlit < 0 || hdist < 0 || hclen < 0) return false;
                std::vector<uint8_t> cl(19, 0);
                for (int i = 0; i < hclen; i++) {
                    int v = br.bits(3);
                    if (v < 0) return false;
                    cl[CL_ORDER[i]] = (uint8_t)v;
                }
                Huff clh;
                clh.build(cl);
                std::vector<uint8_t> lens;
                lens.reserve(hlit + hdist);
                while ((int)lens.size() < hlit + hdist) {
                    int sym = clh.decode(br);
                    if (sym < 0) return false;
                    if (sym < 16) lens.push_back((uint8_t)sym);
                    else if (sym == 16) {
                        if (lens.empty()) return false;
                        int rep = br.bits(2) + 3;
                        if (rep < 0) return false;
                        uint8_t last = lens.back();
                        while (rep-- > 0) lens.push_back(last);
                    } else if (sym == 17) {
                        int rep = br.bits(3) + 3;
                        if (rep < 0) return false;
                        while (rep-- > 0) lens.push_back(0);
                    } else {
                        int rep = br.bits(7) + 11;
                        if (rep < 0) return false;
                        while (rep-- > 0) lens.push_back(0);
                    }
                }
                if ((int)lens.size() < hlit + hdist) return false;
                std::vector<uint8_t> ll(lens.begin(), lens.begin() + hlit);
                std::vector<uint8_t> dl(lens.begin() + hlit, lens.begin() + hlit + hdist);
                lit.build(ll);
                dist.build(dl);
            }
            for (;;) {
                int sym = lit.decode(br);
                if (sym < 0) return false;
                if (sym < 256) {
                    out.push_back((uint8_t)sym);
                } else if (sym == 256) {
                    break;
                } else {
                    int li = sym - 257;
                    if (li >= 29) return false;
                    int len = LEN_BASE[li] + br.bits(LEN_EXTRA[li]);
                    int ds = dist.decode(br);
                    if (len < 0 || ds < 0 || ds >= 30) return false;
                    int d = DIST_BASE[ds] + br.bits(DIST_EXTRA[ds]);
                    if (d < 0 || d > (int)out.size()) return false;
                    size_t from = out.size() - d;
                    for (int i = 0; i < len; i++) out.push_back(out[from + i]);
                }
            }
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace mini
