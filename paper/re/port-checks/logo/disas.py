import sys, capstone
SEG=open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()[0xd00:]
a=int(sys.argv[1],16); n=int(sys.argv[2],16)
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for i in md.disasm(SEG[a:a+n], a): print('%06x: %s %s'%(i.address,i.mnemonic,i.op_str))
