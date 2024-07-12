TEXT entry(SB),0,$-4
	MOV	$0x10004000,R2   // 16KB
	MOV	$setSB(SB),R3

	MOV	$edata(SB),R8	 // clear bss area
	MOV	$end(SB),R5
	MOV	R0,0(R8)
	ADD	$4,R8,R8
	BLT	R5,R8,-2(PC)

	JAL	R1,main(SB)
	JMP	0(PC)

	END
