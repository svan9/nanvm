# NAN VIRTUAL MACHINE

Usage:
```
> ./nanvm <path/to/file>
```
Flags:
```
-h, --help      Display help page
--version       Display current vm version
```

## CODE

### ALL INSTRUCIONS
> NONE LDLL CALL PUSH POP RPOP ADD SUB MUL DIV INC DEC XOR OR NOT AND LS RS NUM INT FLT DBL UINT BYTE MEM REG HEAP ST JMP RET EXIT TEST JE JEL JEM JNE GETCH
	GETCH <ARG> JM MOV SWAP MSET SWST WRITE READ OPEN LM PUTC
wait pressed char and write into arg
> NONE LDLL CALL PUSH POP RPOP ADD SUB MUL DIV INC DEC XOR OR NOT AND LS RS NUM INT FLT DBL UINT BYTE MEM REG	PUTC <CHAR:WCHAR> HEAP ST JMP RET EXIT TEST JE JEL JEM JNE JL JM MOV SWAP MSET SWST WRITE READ OPEN LM PUTC
 PUTI PUTS GETCH MOVRDI 

### TYPES
	FLT NUM - numbers [WORD]
	MEM - heap data number [WORD]
	REG - register [BYTE+BYTE](type+idx)
	ST - stack offset [WORD]

### REGISTERS 
	[int    : 4b] r1  r2  r3  r4  r5
	[float	: 4b] fx1 fx2 fx3 fx4 fx5
	[double	: 8b] dx1 dx2 dx3 dx4 dx5
	[int    : 8b] rx1 rx2 rx3 rx4 rx5

Types:
```cpp
enum struct VM_RegType: byte {
	None,  // 0 
	R,     // 1
	RX,    // 2
	DX,    // 3
	FX     // 4
};
```

### ARG
 	<TYPE> <VALUE>
| Type   | Size   |
|--------|--------|
| ST     | WORD   | 
| REG    | REG    | 
| NUM    | WORD   | 

### NONE
ZERO BYTE
### LDLL
### CALL
### PUSH
	PUSH <TYPE> <NUMBER> 
> push value into stack 
### POP
	POP
> POP value from stack 
### RPOP
	RPOP <REGISTER>
> POP value from stack into register 
### ADD
	ADD <ARG> <ARG>
### SUB
	ADD <ARG> <ARG>
### MUL
	ADD <ARG> <ARG>
### DIV
	ADD <ARG> <ARG>
### INC
	ADD <ARG>
### DEC
	ADD <ARG>
### XOR
	ADD <ARG> <ARG>
### OR
	ADD <ARG> <ARG>
### NOT
	ADD <ARG> 
### AND
	ADD <ARG> <ARG>
### LS
	ADD <ARG> <ARG>
### RS
	ADD <ARG> <ARG>
### JMP
	JMP <OFFSET>
### RET
	RET
### TEST
	TEST <ARG> <ARG>
### JE
	JE <OFFSET>
### JEL
	JEL <OFFSET>
### JEM
	JEM <OFFSET>
### JNE
	JNE <OFFSET>
### JL
	JL <OFFSET>
### JM
	JM <OFFSET>
### MOV
	MOV <ARG> <ARG>
### SWAP
	SWAP <ARG> <ARG>
### MSET
	MSET <start> <size> <value>
### SWST
	SWST <USE_STACK:BYTE> <value?>
	SWST <TRUE> -NUMBER:STACK-
	SWST <FALSE> <NUMBER>
sets current stream
### WRITE
	WRITE <OFFSET:HEAP>
write from heap to current stream
### READ
	READ <OFFSET:HEAP> <CHUNK_SIZE>
read from current stream to heap
### OPEN
	OPEN <OFFSET:HEAP> <FLAGS:INT>
opens file with flag by path and return dest into the stack
### GETCH
	GETCH <ARG>
wait pressed char and write into arg
### PUTC
	PUTC <CHAR:WCHAR>
writes char into default output stream
### PUTI
	PUTI <ARG>
writes integer into default output stream
### PUTS
	PUTS <OFFSET:HEAP>
writes string into default output stream

<div style="text-align: right; font-style: italic">
prod by <b>nansotu studio</b>© developer <b>so2u</b>
</div>