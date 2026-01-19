// SPDX-License-Identifier: MIT
// MyCPU is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

package riscv

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec

// QR code VGA test - verifies QR generation and memory writes
class QRCodeVGATest extends AnyFlatSpec with ChiselScalatestTester {
  behavior.of("[CPU] QR Code VGA program")
  
  it should "generate QR code and write data to memory" in {
    test(new TestTopModule("qrcode_vga.asmbin")).withAnnotations(TestAnnotations.annos) { c =>
      // Run until QR code generation completes
      c.clock.setTimeout(0)
      for (i <- 1 to 1000) {
        c.clock.step(10000)
      }
      
      // Read data in same order as write_qr_data_to_memory():
      // 1. Return value at mem[4]
      c.io.mem_debug_read_address.poke(0x10.U)
      c.clock.step()
      val retValue = c.io.mem_debug_read_data.peekInt()
      println(s"Return value: $retValue")
      c.io.mem_debug_read_data.expect(0.U, "QR generation should succeed (ret=0)")
      
      // 2. QR size at mem[5]
      c.io.mem_debug_read_address.poke(0x14.U)
      c.clock.step()
      val qrSize = c.io.mem_debug_read_data.peekInt()
      println(s"QR size: $qrSize")
    //   c.io.mem_debug_read_data.expect(29.U, "QR size should be 29 (version 3)")
      
      // 3. Bitmap data at mem[6] onwards (29 words)
      println("\nQR Code Bitmap (29 lines):")
      for (i <- 0 until 29) {
        c.io.mem_debug_read_address.poke((0x18 + i * 4).U)
        c.clock.step()
        val lineData = c.io.mem_debug_read_data.peekInt()
        println(f"Line $i%d: 0x$lineData%08X")
      }
      
      // Validate finder patterns
      c.io.mem_debug_read_address.poke(0x18.U)
      c.clock.step()
      val line0 = c.io.mem_debug_read_data.peekInt()
      assert((line0 & 0xFF000000L) == 0xFE000000L, s"Line 0 should start with 0xFE (got 0x${line0.toString(16)})")
      
      c.io.mem_debug_read_address.poke(0x30.U)
      c.clock.step()
      val line6 = c.io.mem_debug_read_data.peekInt()
      assert((line6 & 0xFF000000L) == 0xFE000000L, s"Line 6 should start with 0xFE (got 0x${line6.toString(16)})")
      
      // 4. Completion marker at mem[36]
      c.io.mem_debug_read_address.poke(0x90.U)
      c.clock.step()
      c.io.mem_debug_read_data.expect(0xDEADBEEFL.U, "Completion marker should be 0xDEADBEEF")
    }
  }
}
