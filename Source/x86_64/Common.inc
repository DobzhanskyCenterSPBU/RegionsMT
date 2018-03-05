BITS 64
DEFAULT REL

%ifidn __OUTPUT_FORMAT__, elf64

    %define SYSTEM_V
    %define DECORATE(x) x

%elifidn __OUTPUT_FORMAT__, macho64

    %define SYSTEM_V
    %define DECORATE(x) _ %+ x

%elifidn __OUTPUT_FORMAT__, win64

    %define WINDOWS_64
    %define DECORATE(x) x

%endif