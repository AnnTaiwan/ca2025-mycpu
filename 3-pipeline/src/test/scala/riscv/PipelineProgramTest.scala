// SPDX-License-Identifier: MIT
// MyCPU is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

package riscv

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec
import riscv.core.CSRRegister

class PipelineProgramTest extends AnyFlatSpec with ChiselScalatestTester {
  private val mcauseAcceptable: Set[BigInt] =
    Set(BigInt("80000007", 16), BigInt("8000000B", 16))

  private def runProgram(exe: String, cfg: PipelineConfig)(body: TestTopModule => Unit): Unit = {
    test(new TestTopModule(exe, cfg.implementation))
      .withAnnotations(TestAnnotations.annos) { c =>
        c.io.csr_debug_read_address.poke(0.U)
        c.io.interrupt_flag.poke(0.U)
        body(c)
      }
  }

  for (cfg <- PipelineConfigs.All) {
    behavior.of(cfg.name)

    it should "do uf8_decode/encode from 0-255" in {
      runProgram("q1-uf8_rv32.asmbin", cfg) { c =>
      c.clock.setTimeout(0)  // Disable timeout
        for (i <- 1 to 1000) { // Allow program execution to complete
          c.clock.step(1000)
        }
        for (i <- 0 to 255) { // see each result are they correct or not
          /*
          In mem:
          mem[4] = input, 0
          mem[8] = encoded result, 0
          mem[12] = input, 1
          mem[16] = encoded result, 1
          ...
          mem[2052] = test result, 1(pass), 0(fail)
          */
          c.io.mem_debug_read_address.poke((4 + i * 8).U)
          c.clock.step()
          val input = c.io.mem_debug_read_data.peek().litValue

          c.io.mem_debug_read_address.poke((8 + i * 8).U)  // Encoded results
          c.clock.step()
          val encoded = c.io.mem_debug_read_data.peek().litValue

          // Print values
          // if (i > 250)
            // println(f"Iteration $i%3d: input=0x$input%02x ($input%3d), encoded=0x$encoded%02x ($encoded%3d)")

          // compare, it should be the same
          assert(input == encoded, s", Iteration $i: (input=$input) != (encoded=$encoded)")
        }
        c.io.mem_debug_read_address.poke(2052.U)
        c.clock.step()
        val testResult = c.io.mem_debug_read_data.peek().litValue
        c.io.mem_debug_read_data.expect(1.U, s", UF8 Test failed: result=$testResult") // it should pass.

        // see if x9(s1) is 1 or not
        c.io.regs_debug_read_address.poke(9.U)
        c.io.regs_debug_read_data.expect(1.U)
      }
    }

    it should "do fast_rsqrt" in {
      runProgram("fast_rsqrt.asmbin", cfg) { c =>
      c.clock.setTimeout(0)  // Disable timeout
        for (i <- 1 to 250) { // Allow program execution to complete
          c.clock.step(1000)
        }
        val arr = Array( 
          65536,
          32768,
          16384,
          14654,
          6553,
          4080,
          2570
        )
        for (i <- 0 until arr.length) { // see each result are they correct or not
          /*
          In mem:
          mem[4] = test result, 1(pass), 0(fail)
          mem[8] = result 1
          mem[12] = result 2
          mem[16] = result 3
          ...
          */
          c.io.mem_debug_read_address.poke((8 + i * 4).U)
          c.clock.step()
          val result = c.io.mem_debug_read_data.peek().litValue
          
          // println(f"Iteration $i%3d: fast_rsqrt_result=0x$result%x ($result%d), answer=0x${arr(i)}%x (${arr(i)}%d)")
         
          c.io.mem_debug_read_data.expect(arr(i).U)
        }
        c.io.mem_debug_read_address.poke(4.U)
        c.clock.step()
        val testResult = c.io.mem_debug_read_data.peek().litValue
        c.io.mem_debug_read_data.expect(1.U, s", fast_rsqrt Test failed: result=$testResult") // it should pass.
      }
    }
    
    it should "calculate recursively fibonacci(10)" in {
      runProgram("fibonacci.asmbin", cfg) { c =>
        for (i <- 1 to 50) {
          c.clock.step(1000)
          c.io.mem_debug_read_address.poke((i * 4).U)
        }
        c.io.mem_debug_read_address.poke(4.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(55.U)
      }
    }

    it should "quicksort 10 numbers" in {
      runProgram("quicksort.asmbin", cfg) { c =>
        for (i <- 1 to 50) {
          c.clock.step(1000)
          c.io.mem_debug_read_address.poke((i * 4).U)
        }
        for (i <- 1 to 10) {
          c.io.mem_debug_read_address.poke((4 * i).U)
          c.clock.step()
          c.io.mem_debug_read_data.expect((i - 1).U)
        }
      }
    }

    it should "store and load single byte" in {
      runProgram("sb.asmbin", cfg) { c =>
        c.clock.step(1000)
        c.io.regs_debug_read_address.poke(5.U)
        c.io.regs_debug_read_data.expect(0xdeadbeefL.U)
        c.io.regs_debug_read_address.poke(6.U)
        c.io.regs_debug_read_data.expect(0xef.U)
        c.io.regs_debug_read_address.poke(1.U)
        c.io.regs_debug_read_data.expect(0x15ef.U)
      }
    }

    it should "solve data and control hazards" in {
      runProgram("hazard.asmbin", cfg) { c =>
        c.clock.step(1000)
        c.io.regs_debug_read_address.poke(1.U)
        c.io.regs_debug_read_data.expect(cfg.hazardX1.U)
        c.io.mem_debug_read_address.poke(4.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(1.U)
        c.io.mem_debug_read_address.poke(8.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(3.U)
      }
    }

    it should "handle all hazard types comprehensively" in {
      runProgram("hazard_extended.asmbin", cfg) { c =>
        c.clock.step(1000)

        // Section 1: WAW (Write-After-Write) - later write wins
        c.io.mem_debug_read_address.poke(0x10.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(2.U, "WAW: mem[0x10] should be 2 (final write)")

        // Section 2: Store-Load Forwarding
        c.io.mem_debug_read_address.poke(0x14.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(0xab.U, "Store-Load: mem[0x14] should be 0xAB")

        // Section 3: Multiple Consecutive Loads (sum of zeros)
        c.io.mem_debug_read_address.poke(0x18.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(0.U, "Multi-Load: mem[0x18] should be 0")

        // Section 4: Branch Condition RAW (branch not taken)
        c.io.mem_debug_read_address.poke(0x1c.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(10.U, "Branch RAW: mem[0x1C] should be 10")

        // Section 5: JAL Return Address Hazard (skip validation - address varies)
        // Section 6: CSR RAW Hazard (cycle count diff + 0x1888 signature)
        c.io.mem_debug_read_address.poke(0x24.U)
        c.clock.step()
        val csrValue = c.io.mem_debug_read_data.peek().litValue
        assert(
          csrValue >= 0x1888 && csrValue <= 0x1900,
          f"CSR RAW: mem[0x24] should be 0x1888-0x1900, got 0x$csrValue%x"
        )

        // Section 7: Long Dependency Chain (1+2+3+4 = 5)
        c.io.mem_debug_read_address.poke(0x28.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(5.U, "Long Chain: mem[0x28] should be 5")

        // Section 8: WB Stage Forwarding
        c.io.mem_debug_read_address.poke(0x2c.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(7.U, "WB Forward: mem[0x2C] should be 7")

        // Section 9: Load-to-Store Forwarding
        c.io.mem_debug_read_address.poke(0x30.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(0.U, "Load-Store: mem[0x30] should be 0")

        // Section 10: Branch with Multiple RAW (branch not taken)
        c.io.mem_debug_read_address.poke(0x34.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(20.U, "Multi-RAW Branch: mem[0x34] should be 20")

        // Validate cycle count (s0 register = x8)
        c.io.regs_debug_read_address.poke(8.U)
        c.clock.step()
        val cycles = c.io.regs_debug_read_data.peek().litValue
        assert(cycles > 0, s"${cfg.name}: Cycle count should be > 0, got $cycles")
      }
    }

    it should "handle machine-mode traps" in {
      runProgram("irqtrap.asmbin", cfg) { c =>
        c.clock.setTimeout(0)
        for (i <- 1 to 1000) {
          c.clock.step()
          c.io.mem_debug_read_address.poke((i * 4).U)
        }
        c.io.mem_debug_read_address.poke(4.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(0xdeadbeefL.U)

        c.io.interrupt_flag.poke(1.U)
        c.clock.step(5)
        c.io.interrupt_flag.poke(0.U)

        for (i <- 1 to 1000) {
          c.clock.step()
          c.io.mem_debug_read_address.poke((i * 4).U)
        }
        c.io.csr_debug_read_address.poke(CSRRegister.MSTATUS)
        c.clock.step()
        c.io.csr_debug_read_data.expect(0x1888.U)
        c.io.csr_debug_read_address.poke(CSRRegister.MCAUSE)
        c.clock.step()
        val cause = c.io.csr_debug_read_data.peek().litValue
        assert(mcauseAcceptable.contains(cause), f"unexpected mcause 0x${cause}%x")
        c.io.mem_debug_read_address.poke(0x4.U)
        c.clock.step()
        c.io.mem_debug_read_data.expect(0x2022L.U)
      }
    }
    
  }
}
