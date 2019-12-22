#include <elf.h>

static void
write_elf_file(const char* filename) {
    FILE* f = fopen(filename, "w");
    size_t n_phdr = 2;
    size_t n_shdr = 4;
    char strings[] = ".shstrtab\0.text\0.data";

    size_t phdr_offset = sizeof (Elf64_Ehdr);
    size_t text_offset = phdr_offset + sizeof (Elf64_Phdr) * n_phdr;
    size_t data_offset = text_offset + seg_text.len;
    data_offset = ((data_offset - 1) & ~0xfff) + 0x1000;
    size_t strings_offset = data_offset + seg_data.len;
    size_t shdr_offset = strings_offset + sizeof strings;

    const Binding* mainfn = get_binding(STR("main"));
    if (mainfn == NULL) {
        fprintf(stderr, "No main function.\n");
        return;
    }
    assert(mainfn->last_vreg->state == VREG_MEM_ADDR);
    Location loc = mainfn->last_vreg->loc;
    size_t entry = loc.seg->addr + loc.offset;

    Elf64_Ehdr elf_header = {
        .e_ident = {
            '\x7f', 'E', 'L', 'F',
            ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_NONE,
            0, 0, 0, 0,
            0, 0, 0, 0,
        },
        .e_type = ET_EXEC,
        .e_machine = EM_RISCV,
        .e_version = EV_CURRENT,
        .e_entry = entry,
        .e_phoff = phdr_offset,
        .e_shoff = shdr_offset,
        .e_flags = 0,
        .e_ehsize = sizeof (Elf64_Ehdr),
        .e_phentsize = sizeof (Elf64_Phdr),
        .e_phnum = n_phdr,
        .e_shentsize = sizeof (Elf64_Shdr),
        .e_shnum = n_shdr,
        .e_shstrndx = n_shdr - 1,
    };
    Elf64_Phdr elf_program_header[] = {
        {
            .p_type = PT_LOAD,
            .p_flags = PF_R | PF_X,
            .p_offset = text_offset,
            .p_vaddr = seg_text.addr,
            .p_paddr = seg_text.addr,
            .p_filesz = seg_text.len,
            .p_memsz = seg_text.len,
            .p_align = 0x1,
        },
        {
            .p_type = seg_data.len ? PT_LOAD : 0,
            .p_flags = PF_R | PF_W,
            .p_offset = data_offset,
            .p_vaddr = seg_data.addr,
            .p_paddr = seg_data.addr,
            .p_filesz = seg_data.len,
            .p_memsz = seg_data.len,
            .p_align = 0x1,
        },
    };
    Elf64_Shdr elf_section_header[] = {
        {
            .sh_name = sizeof strings - 1,
        },
        {
            .sh_name = 10,
            .sh_type = SHT_PROGBITS,
            .sh_flags = SHF_ALLOC | SHF_EXECINSTR,
            .sh_addr = seg_text.addr,
            .sh_offset = text_offset,
            .sh_size = seg_text.len,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 2,
            .sh_entsize = 0,
        },
        {
            .sh_name = 16,
            .sh_type = SHT_PROGBITS,
            .sh_flags = SHF_ALLOC | SHF_WRITE,
            .sh_addr = seg_data.addr,
            .sh_offset = data_offset,
            .sh_size = seg_data.len,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0,
        },
        {
            .sh_name = 0,
            .sh_type = SHT_STRTAB,
            .sh_flags = 0,
            .sh_addr = 0,
            .sh_offset = strings_offset,
            .sh_size = sizeof strings,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0,
        },
    };
    size_t n = 0;
    n += fwrite(&elf_header, sizeof elf_header, 1, f);
    n += fwrite(&elf_program_header, sizeof elf_program_header, 1, f);
    n += fwrite(seg_text.data, seg_text.len, 1, f);
    fseek(f, data_offset, SEEK_SET);
    n = data_offset;
    n += fwrite(seg_data.data, seg_data.len, 1, f);
    n += fwrite(strings, 1, sizeof strings, f);
    n += fwrite(&elf_section_header, sizeof elf_section_header, 1, f);
    struct stat statbuf;
    fstat(fileno(f), &statbuf);
    fchmod(fileno(f), statbuf.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
    fclose(f);
}
