/* The default OG bitstream, embedded rather than compiled from a 1 MB C
   header. .incbin is why fpga_default_v5.bin is checked in as a binary. */
    .section .rodata.fwog_fpga_bitstream, "a"
    .global fwog_fpga_bitstream
    .balign 4
fwog_fpga_bitstream:
    .incbin "fpga_default_v5.bin"
    .global fwog_fpga_bitstream_end
fwog_fpga_bitstream_end:
