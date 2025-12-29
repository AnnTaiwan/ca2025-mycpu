// SPDX-License-Identifier: MIT
// MyCPU is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

package riscv.singlecycle

import java.nio.ByteBuffer
import java.nio.ByteOrder

import chisel3._
import chisel3.util._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec
import peripheral.InstructionROM
import peripheral.Memory
import peripheral.ROMLoader
import riscv.core.CPU
import riscv.core.CSRRegister
import riscv.core.ProgramCounter
import riscv.Parameters
import riscv.TestAnnotations

class TestTopModule(exeFilename: String) extends Module {
  val io = IO(new Bundle {
    val mem_debug_read_address      = Input(UInt(Parameters.AddrWidth))
    val regs_debug_read_address     = Input(UInt(Parameters.PhysicalRegisterAddrWidth))
    val csr_regs_debug_read_address = Input(UInt(Parameters.CSRRegisterAddrWidth))
    val interrupt_flag              = Input(UInt(Parameters.InterruptFlagWidth))

    val regs_debug_read_data     = Output(UInt(Parameters.DataWidth))
    val mem_debug_read_data      = Output(UInt(Parameters.DataWidth))
    val csr_regs_debug_read_data = Output(UInt(Parameters.DataWidth))
    val pc_debug_read            = Output(UInt(Parameters.AddrWidth))
  })

  val mem             = Module(new Memory(8192))
  val instruction_rom = Module(new InstructionROM(exeFilename))
  val rom_loader      = Module(new ROMLoader(instruction_rom.capacity))

  rom_loader.io.rom_data     := instruction_rom.io.data
  rom_loader.io.load_address := Parameters.EntryAddress
  instruction_rom.io.address := rom_loader.io.rom_address

  val CPU_clkdiv = RegInit(UInt(2.W), 0.U)
  val CPU_tick   = Wire(Bool())
  val CPU_next   = Wire(UInt(2.W))
  CPU_next   := Mux(CPU_clkdiv === 3.U, 0.U, CPU_clkdiv + 1.U)
  CPU_tick   := CPU_clkdiv === 0.U
  CPU_clkdiv := CPU_next

  withClock(CPU_tick.asClock) {
    val cpu = Module(new CPU)

    cpu.io.instruction_valid := rom_loader.io.load_finished
    cpu.io.interrupt_flag    := io.interrupt_flag

    val cpuMemAddress     = cpu.io.memory_bundle.address
    val cpuMemWriteData   = cpu.io.memory_bundle.write_data
    val cpuMemWriteEnable = cpu.io.memory_bundle.write_enable
    val cpuMemWriteStrobe = cpu.io.memory_bundle.write_strobe
    val cpuMemReadData    = Wire(UInt(Parameters.DataWidth))
    cpu.io.memory_bundle.read_data := cpuMemReadData

    mem.io.instruction_address := cpu.io.instruction_address

    cpu.io.regs_debug_read_address     := io.regs_debug_read_address
    cpu.io.csr_regs_debug_read_address := io.csr_regs_debug_read_address
    io.regs_debug_read_data            := cpu.io.regs_debug_read_data
    io.csr_regs_debug_read_data        := cpu.io.csr_regs_debug_read_data
    io.pc_debug_read                   := cpu.io.instruction_address

    val memAddress     = Wire(UInt(Parameters.AddrWidth))
    val memWriteData   = Wire(UInt(Parameters.DataWidth))
    val memWriteEnable = Wire(Bool())
    val memWriteStrobe = Wire(Vec(Parameters.WordSize, Bool()))
    mem.io.bundle.address      := memAddress
    mem.io.bundle.write_data   := memWriteData
    mem.io.bundle.write_enable := memWriteEnable
    mem.io.bundle.write_strobe := memWriteStrobe

    val fullAddress = Cat(
      cpu.io.deviceSelect,
      cpuMemAddress(Parameters.AddrBits - Parameters.SlaveDeviceCountBits - 1, 0)
    )

    val timerLimit  = RegInit(0.U(32.W))
    val timerEnable = RegInit(false.B)

    val addrHighNibble = fullAddress(31, 28)
    val isTimer        = addrHighNibble === "h8".U
    val isMMIO         = isTimer
    val mmioOffset     = fullAddress(7, 0)

    when(cpuMemWriteEnable && isTimer) {
      when(mmioOffset === 0x04.U) {
        timerLimit := cpuMemWriteData
      }.elsewhen(mmioOffset === 0x08.U) {
        timerEnable := cpuMemWriteData =/= 0.U
      }
    }

    val timerReadData = MuxCase(
      0.U(32.W),
      Seq(
        (mmioOffset === 0x04.U) -> timerLimit,
        (mmioOffset === 0x08.U) -> timerEnable.asUInt
      )
    )
    val mmioReadData = Mux(isTimer, timerReadData, 0.U)

    when(!rom_loader.io.load_finished) {
      memAddress                     := rom_loader.io.bundle.address
      memWriteData                   := rom_loader.io.bundle.write_data
      memWriteEnable                 := rom_loader.io.bundle.write_enable
      memWriteStrobe                 := rom_loader.io.bundle.write_strobe
      rom_loader.io.bundle.read_data := mem.io.bundle.read_data
      cpuMemReadData                 := 0.U
    }.otherwise {
      rom_loader.io.bundle.read_data := 0.U
      memAddress                     := fullAddress
      memWriteData                   := cpuMemWriteData
      memWriteEnable                 := cpuMemWriteEnable && !isMMIO
      memWriteStrobe                 := cpuMemWriteStrobe
      cpuMemReadData                 := Mux(isMMIO, mmioReadData, mem.io.bundle.read_data)
    }

    cpu.io.instruction := mem.io.instruction
  }

  mem.io.debug_read_address := io.mem_debug_read_address
  io.mem_debug_read_data    := mem.io.debug_read_data
}

class FibonacciTest extends AnyFlatSpec with ChiselScalatestTester {
  behavior.of("[CPU] Fibonacci program")
  it should "calculate recursively fibonacci(10)" in {
    test(new TestTopModule("fibonacci.asmbin")).withAnnotations(TestAnnotations.annos) { c =>
      for (i <- 1 to 50) {
        c.clock.step(1000)
        c.io.mem_debug_read_address.poke((i * 4).U) // Avoid timeout
      }

      c.io.mem_debug_read_address.poke(4.U)
      c.clock.step()
      c.io.mem_debug_read_data.expect(55.U)
    }
  }
}

class QuicksortTest extends AnyFlatSpec with ChiselScalatestTester {
  behavior.of("[CPU] Quicksort program")
  it should "quicksort 10 numbers" in {
    test(new TestTopModule("quicksort.asmbin")).withAnnotations(TestAnnotations.annos) { c =>
      for (i <- 1 to 50) {
        c.clock.step(1000)
        c.io.mem_debug_read_address.poke((i * 4).U) // Avoid timeout
      }
      for (i <- 1 to 10) {
        c.io.mem_debug_read_address.poke((4 * i).U)
        c.clock.step()
        c.io.mem_debug_read_data.expect((i - 1).U)
      }
    }
  }
}

class ByteAccessTest extends AnyFlatSpec with ChiselScalatestTester {
  behavior.of("[CPU] Byte access program")
  it should "store and load single byte" in {
    test(new TestTopModule("sb.asmbin")).withAnnotations(TestAnnotations.annos) { c =>
      for (i <- 1 to 500) {
        c.clock.step()
        c.io.mem_debug_read_address.poke((i * 4).U) // Avoid timeout
      }
      c.io.regs_debug_read_address.poke(5.U)
      c.io.regs_debug_read_data.expect(0xdeadbeefL.U)
      c.io.regs_debug_read_address.poke(6.U)
      c.io.regs_debug_read_data.expect(0xef.U)
      c.io.regs_debug_read_address.poke(1.U)
      c.io.regs_debug_read_data.expect(0x15ef.U)
    }
  }
}

class InterruptTrapTest extends AnyFlatSpec with ChiselScalatestTester {
  behavior.of("[CPU] Interrupt trap flow")
  it should "jump to trap handler and then return" in {
    test(new TestTopModule("irqtrap.asmbin")).withAnnotations(TestAnnotations.annos) { c =>
      for (i <- 1 to 1000) {
        c.clock.step()
        c.io.mem_debug_read_address.poke((i * 4).U) // Avoid timeout
      }
      c.io.mem_debug_read_address.poke(4.U)
      c.clock.step()
      c.io.mem_debug_read_data.expect(0xdeadbeefL.U)
      c.io.interrupt_flag.poke(0x1.U)
      c.clock.step(5)
      c.io.interrupt_flag.poke(0.U)
      for (i <- 1 to 1000) {
        c.clock.step()
        c.io.mem_debug_read_address.poke((i * 4).U) // Avoid timeout
      }
      c.io.csr_regs_debug_read_address.poke(CSRRegister.MSTATUS)
      c.io.csr_regs_debug_read_data.expect(0x1888.U)
      c.io.csr_regs_debug_read_address.poke(CSRRegister.MCAUSE)
      c.io.csr_regs_debug_read_data.expect(0x80000007L.U)
      c.io.mem_debug_read_address.poke(0x4.U)
      c.clock.step()
      c.io.mem_debug_read_data.expect(0x2022L.U)
    }
  }
}

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
      c.io.mem_debug_read_data.expect(29.U, "QR size should be 29 (version 3)")
      
      // 3. Bitmap data at mem[6] onwards (29 words)
      println("\nQR Code Bitmap (29 lines):")
      for (i <- 0 until 29) {
        c.io.mem_debug_read_address.poke((0x18 + i * 4).U)
        c.clock.step()
        val lineData = c.io.mem_debug_read_data.peekInt()
        println(f"Line $i%2d: 0x$lineData%08X")
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
      
      // 5. ASCII QR code at mem[64] onwards
      println("\n========== ASCII QR Code ==========")
      val asciiStart = 0x100
      var shouldBreak = false
      for (i <- 0 until 3500 by 4 if !shouldBreak) {
        c.io.mem_debug_read_address.poke((asciiStart + i).U)
        c.clock.step()
        val word = c.io.mem_debug_read_data.peekInt().toInt
        for (j <- 0 until 4 if !shouldBreak) {
          val ch = ((word >> (j * 8)) & 0xFF).toChar
          if (ch == '\u0000') {
            shouldBreak = true
          } else {
            print(ch)
          }
        }
      }
      println("\n===================================")
    }
  }
}
