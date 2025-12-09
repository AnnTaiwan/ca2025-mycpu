# Perform uf8_encode and uf8_decode and CLZ in rv32i
.section .text
.globl _start
# Function: _start
# Description: Main entry point that tests UF8 encode/decode functions for all 
#              values from 0 to 255. Stores input values, decoded results, and 
#              encoded results to memory for validation. Also checks two conditions:
#              1. Encoded result must match original input (encode(decode(x)) == x)
#              2. Decoded values must be monotonically increasing
#
# Memory Layout (stored sequentially starting at address 4):
#   - For each i in [0, 255]:
#       mem[4 + i*8]     : Original encoded value (i)
#       mem[4 + i*8 + 4] : Re-encoded result after decode->encode cycle
#   - mem[4 + 256*8]     : Test result (1 = all tests passed, 0 = at least one failed)
#
# Example Memory Map:
#   mem[0x004] = 0     (input for iteration 0)
#   mem[0x008] = 0     (re-encoded output for iteration 0)
#   mem[0x00C] = 1     (input for iteration 1)
#   mem[0x010] = 1     (re-encoded output for iteration 1)
#   ...
#   mem[0x804] = 255   (input for iteration 255)
#   mem[0x808] = 255   (re-encoded output for iteration 255)
#   mem[0x80C] = 1/0   (final test result: passed/failed)
#
# Registers Used:
#   s0 - previous_value: Tracks decoded value from previous iteration (for monotonic check)
#   s1 - passed: Test status flag (1 = passing, 0 = failed)
#   s2 - f1: Loop counter (0 to 255)
#   s3 - Loop end value (256)
#   s4 - value: Current decoded value from uf8_decode
#   s5 - fl2: Current re-encoded value from uf8_encode
#   s7 - mem_addr: Memory write pointer (starts at 4, increments by 4)
#   ra - Return address (saved on stack)
#
# Input: None
#
# Output: 
#   Memory contains test data and final result at mem[0x80C]
# 
# End:
#   enter infinite loop
# Stack Usage: 4 bytes (stores return address)
_start:
    addi sp, sp, -4
    sw ra, 0(sp) # because test need to call other function
    addi s0, x0, -1 # previous_value
    li s1, 1 # passed, 1 means true, 0 means false
    li s2, 0 # f1, counter from 0 to 255
    li s3, 256 # counter's end
    li s7, 4 # mem direct address for later sw at here, start from 4
For_2:
    add a0, s2, x0 # prepare a0 for uf8_decode
    # store original input(encoded one)
    sw a0, 0(s7) # store at s7[0], s7 is 4, 12, 20, ...
    addi s7, s7, 4

    jal ra, uf8_decode
    add s4, a0, x0 # value (return value from uf8_decode)
    add a0, s4, x0 # prepare a0 for uf8_encode
    jal ra, uf8_encode
    add s5, a0, x0 # fl2 (return value from uf8_encode)
    # store encoded result
    sw a0, 0(s7) # store at s7[0], s7 is 4, 12, 20, ...
    addi s7, s7, 4

test_if_1:
    beq s2, s5, test_if_2
    # if not pass
    li s1, 0 # passed = false
test_if_2:
    blt s0, s4, after_if
    # if not pass
    li s1, 0 # passed = false
after_if:
    mv s0, s4
    addi s2, s2, 1
    blt s2, s3, For_2
    # store test result: 0 or 1
    sw s1, 0(s7)

    lw ra, 0(sp)
    addi sp, sp, 4
loop:
    j loop    # Infinite loop

# Function: CLZ
# Description: Counts the number of leading zero bits in a 32-bit unsigned integer
#              using binary search algorithm
# 
# Input:
#   a0 - 32-bit unsigned integer value to count leading zeros
#
# Output:
#   a0 - Number of leading zero bits (0-32)
CLZ:
    li t0, 32 #n
    li t1, 16 #c
do_while:
    srl t2, a0, t1 #t2 is y
    beq t2, x0, SHIFT_TOO_MUCH
    sub t0, t0, t1
    add a0, t2, x0
SHIFT_TOO_MUCH: 
    srli t1, t1, 1
    bne t1, x0, do_while
    sub a0, t0, a0
    jr ra   # jump to ra

# Function: uf8_decode
# Description: Decodes an 8-bit UF8 (micro-float 8) encoded value into 
#              a 20-bit unsigned integers
#
# UF8 Format (8 bits):
#   [7:4] - exponent (4 bits)
#   [3:0] - mantissa (4 bits)
#
# Input:
#   a0 - 8-bit UF8 encoded value
#
# Output:
#   a0 - 20-bit decoded unsigned integers
# .globl uf8_decode
.type uf8_decode,%function
.align 2   
uf8_decode:
    andi t0, a0, 0x0F # mantissa
    srli t1, a0, 4 # exponent
    li t2, 15
    sub t2, t2, t1 # 15 - exponent
    li t3, 0x7FFF
    srl t3, t3, t2
    slli t3, t3, 4 # offset
    sll t2, t0, t1
    add a0, t2, t3
    jr ra   # jump to ra
.size uf8_decode,.-uf8_decode

# Function: uf8_encode
# Description: Encodes a 20-bit unsigned integers into an 8-bit UF8 
#
# UF8 Format (8 bits):
#   [7:4] - exponent (4 bits)
#   [3:0] - mantissa (4 bits)
#
# Input:
#   a0 - 20-bit unsigned integers to encode
#
# Output:
#   a0 - 8-bit UF8 encoded value
# .globl uf8_encode
.type uf8_encode,%function
.align 2
uf8_encode:
    addi sp, sp, -4
    sw ra, 0(sp) # because it will call CLZ in this function
    add t6, a0, x0 # value
    li t0, 16
    blt t6, t0, RETURN # if value < 16
    jal ra, CLZ # call clz
    li t0, 31
    sub t0, t0, a0 # msb,  a0 is lz(return value from CLZ)
    add t1, a0, x0 # lz

    li t2, 0 # exponent # Start from a good initial guess
    li t3, 0 # overflow
    li t4, 5
    blt t0, t4, Find_exact_exponent # go to Find_exact_exponent
    addi t2, t0, -4
    li t4, 15
    bge t4, t2, Cal_overflow # if 15 >= exponent
    li t2, 15 # exponent is 15
Cal_overflow:
    li t4, 0 # counter
For_1:
    slli t5, t3, 1
    addi t3, t5, 16 # overflow = (overflow << 1) + 16;
    addi t4, t4, 1
    blt t4, t2, For_1
while_1:
    blez t2, Find_exact_exponent
    bge  t6, t3, Find_exact_exponent
    addi t5, t3, -16 
    srli t3, t5, 1 # overflow = (overflow - 16) >> 1;
    addi t2, t2, -1
    j while_1
Find_exact_exponent:    
    li t5, 15
while_2: 
    bge t2, t5, PRE_RETURN
    slli t4, t3, 1
    addi t4, t4, 16 # next_overflow = (overflow << 1) + 16;
    blt t6, t4, PRE_RETURN
    add t3, t4, x0
    addi t2, t2, 1
    j while_2
PRE_RETURN:
    sub t1, t6, t3
    srl t1, t1, t2 # mantissa
    slli t0, t2, 4
    or a0, t0, t1 # prepare return value(a0)
RETURN:
    lw ra, 0(sp)
    addi sp, sp, 4
    jr ra   # jump to ra
.size uf8_encode,.-uf8_encode
