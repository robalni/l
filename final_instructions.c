// Returns the bits of x between (including) position l and h.
// Example: BITS(0b1100101, 2, 5) == 1001
#define BITS(x, l, h) ((x & ((1 << ((h + 1) % (sizeof (x) * 8))) - 1)) >> l)

static void
rv64_write_add(Segment* seg, enum reg rd, enum reg rs1, enum reg rs2) {
    uint32_t i;
    if (rs2 == rd) {
        rs2 = rs1;
        rs1 = rd;
    }
    if (rs1 == REG_ZERO) {
        i = 0b1000000000000010 | (rs2 << 2) | (rd << 7);
        add_data(seg, &i, 2);
    } else if (rs1 == rd) {
        i = 0b1001000000000010 | (rs2 << 2) | (rd << 7);
        add_data(seg, &i, 2);
    } else {
        i = 0b0110011 | (rd << 7) | (rs1 << 15) | (rs2 << 20);
        add_data(seg, &i, 4);
    }
}

static void
rv64_write_mul(Segment* seg, enum reg rd, enum reg rs1, enum reg rs2) {
    uint32_t i;
    i = 0b0110011 | rd << 7 | rs1 << 15 | rs2 << 20 | 1 << 25;
    add_data(seg, &i, 4);
}

static void
rv64_write_li(Segment* seg, enum reg rd, int16_t n) {
    uint32_t i;
    if ((n & 0b111111) == n) {
        i = 0b0100000000000001 | (rd << 7) | (BITS(n, 0, 4) << 2)
            | (BITS(n, 5, 5) << 12);
        add_data(seg, &i, 2);
    } else {
        i = 0b0010011 | (rd << 7) | (BITS(n, 0, 11) << 20);
        add_data(seg, &i, 4);
    }
}

static void
rv64_write_lui(Segment* seg, enum reg rd, int32_t addr) {
    uint32_t i;
    if (addr < (1 << 18)) {
        i = 0b0110000000000001 | (rd << 7) | (BITS(addr, 12, 16) << 2)
            | (BITS(addr, 17, 17) << 12);
        add_data(seg, &i, 2);
    } else {
        i = 0b0110111 | (rd << 7) | (BITS(addr, 12, 31) << 12);
        add_data(seg, &i, 4);
    }
}

static void
rv64_write_lw(Segment* seg, enum reg rd, uint16_t addr, enum reg rs1) {
    uint32_t i;
    i = 0b010000000000011 | (rd << 7) | (rs1 << 15)
        | (BITS(addr, 0, 11) << 20);
    add_data(seg, &i, 4);
}

static void
rv64_write_sw(Segment* seg, enum reg rs2, uint16_t addr, enum reg rs1) {
    uint32_t i;
    i = 0b010000000100011 | (BITS(addr, 0, 4) << 7) | (rs1 << 15)
        | (rs2 << 20) | (BITS(addr, 5, 11) << 25);
    add_data(seg, &i, 4);
}

static void
rv64_write_jump(Segment* seg, int16_t off) {
    uint16_t n;
    n = 0b1010000000000001
        | ((off >> 5 & 1) << 2)
        | ((off >> 1 & 7) << 3)
        | ((off >> 7 & 1) << 6)
        | ((off >> 6 & 1) << 7)
        | ((off >> 10 & 1) << 8)
        | ((off >> 8 & 3) << 9)
        | ((off >> 4 & 1) << 11)
        | ((off >> 11 & 1) << 12);
    add_data(seg, &n, 2);
}

static void
rv64_write_ecall(Segment* seg) {
    uint32_t i;
    i = 0b1110011;
    add_data(seg, &i, 4);
}
