// RISCV microcontroller, extracted from the RVSoC project of the Kise Lab of
// the University of Tokyo. BSD License.


// 2-ported register file
//
module m_regfile (CLK, w_rs1, w_rs2, w_rdata1, w_rdata2, w_we, rd, w_wdata);
    input  wire        CLK;
    input  wire [ 4:0] w_rs1, w_rs2;
    output wire [31:0] w_rdata1, w_rdata2;
    input  wire        w_we;
    input  wire [ 4:0] rd;
    input  wire [31:0] w_wdata;

    reg [31:0] mem [0:31];

    assign w_rdata1 = (w_rs1 == 0) ? 0 : mem[w_rs1];
    assign w_rdata2 = (w_rs2 == 0) ? 0 : mem[w_rs2];

    always @(posedge CLK) begin
        if(w_we) begin
            if(rd!=0) mem[rd] <= w_wdata;
        end
    end

    wire [31:0] reg8 = mem[8];
    wire [31:0] reg9 = mem[9];
    wire [31:0] reg10 = mem[10];
    wire [31:0] reg11 = mem[11];
    wire [31:0] reg12 = mem[12];
    wire [31:0] reg13 = mem[13];

    integer i;
    initial begin
      for(i=0; i<32; i=i+1) mem[i] = 0;
    end
endmodule

// Pipeline stages
//
`define MC_IF  0     // Inst Fetch + Write-back
`define MC_OF  1     // Operand Fetch
`define MC_EX  2     // Execution
`define MC_MA  3     // Memory Access

// Immediates generator
//
`define OPCODE_OP______ 7'h33
`define OPCODE_OP_IMM__ 7'h13
`define OPCODE_LOAD____ 7'h03
`define OPCODE_JALR____ 7'h67
`define OPCODE_STORE___ 7'h23
`define OPCODE_BRANCH__ 7'h63
`define OPCODE_LUI_____ 7'h37
`define OPCODE_AUIPC___ 7'h17
`define OPCODE_JAL_____ 7'h6F
`define OPCODE_JALR____ 7'h67
`define OPCODE_MISC_MEM 7'h0F
`define OPCODE_SYSTEM__ 7'h73

module m_imm_gen(w_inst, r_imm);
    input  wire [31:0]  w_inst;
    output reg  [31:0]  r_imm;
    
    wire [6:0] opcode = w_inst[6:0];
    wire [31:0] imm_I = { {21{w_inst[31]}}, w_inst[30:25], w_inst[24:20] };
    wire [31:0] imm_S = { {21{w_inst[31]}}, w_inst[30:25], w_inst[11:8], w_inst[7] };
    wire [31:0] imm_B = { {20{w_inst[31]}}, w_inst[7],w_inst[30:25] ,w_inst[11:8], 1'b0 };
    wire [31:0] imm_U = { w_inst[31:12], 12'b0 };
    wire [31:0] imm_J = { {12{w_inst[31]}}, w_inst[19:12], w_inst[20], w_inst[30:25], w_inst[24:21], 1'b0 };
    wire [31:0] imm_C = { 27'b0, w_inst[19:15] };
    
    always @(*) begin
        case (opcode)
            `OPCODE_OP_IMM__ : r_imm = imm_I;
            `OPCODE_STORE___ : r_imm = imm_S;
            `OPCODE_LOAD____ : r_imm = imm_I;
            `OPCODE_LUI_____ : r_imm = imm_U;
            `OPCODE_BRANCH__ : r_imm = imm_B;
            `OPCODE_AUIPC___ : r_imm = imm_U;
            `OPCODE_JAL_____ : r_imm = imm_J;
            `OPCODE_JALR____ : r_imm = imm_I;
            `OPCODE_SYSTEM__ : r_imm = imm_C;
            default          : r_imm = 0;
        endcase
    end
endmodule

// Integer ALU
//
`define FUNCT3_ADD___   3'h0
`define FUNCT3_SLL___   3'h1
`define FUNCT3_SLT___   3'h2
`define FUNCT3_SLTU__   3'h3
`define FUNCT3_XOR___   3'h4
`define FUNCT3_SRL___   3'h5
`define FUNCT3_OR____   3'h6
`define FUNCT3_AND___   3'h7

module m_alu_i (w_in1, w_in2, w_funct3, w_funct7, r_rslt);
    input wire [31:0]   w_in1, w_in2;
    input wire  [2:0]   w_funct3;
    input wire  [6:0]   w_funct7;
    output reg [31:0]   r_rslt;
    
    wire signed [31:0] w_sin1 = w_in1;
    wire signed [31:0] w_sin2 = w_in2;
    
    wire [4:0] w_shamt = w_in2[4:0];
    
    always@(*) begin
        case (w_funct3)
            `FUNCT3_ADD___ : r_rslt = (w_funct7) ? w_in1 - w_in2 : w_in1 + w_in2;
            `FUNCT3_SLL___ : r_rslt = w_in1 << w_shamt;
            `FUNCT3_SLT___ : r_rslt = {31'b0, w_sin1 < w_sin2};
            `FUNCT3_SLTU__ : r_rslt = {31'b0, w_in1 < w_in2};
            `FUNCT3_XOR___ : r_rslt = w_in1 ^ w_in2;
            `FUNCT3_SRL___ : begin
                                if(w_funct7[5]) r_rslt = w_sin1 >>> w_shamt;
                                else            r_rslt = w_in1   >> w_shamt;
                            end
            `FUNCT3_OR____ : r_rslt = w_in1 | w_in2;
            `FUNCT3_AND___ : r_rslt = w_in1 & w_in2;
            default        : begin
                r_rslt = 0;
`ifdef __ICARUS__
                $write("ILLEGAL INSTRUCTION! in alu_i\n");
                $finish();
`endif
            end
        endcase
    end
endmodule

// Branch destination ALU
//
`define FUNCT3_BEQ___   3'h0
`define FUNCT3_BNE___   3'h1
`define FUNCT3_BLT___   3'h4
`define FUNCT3_BGE___   3'h5
`define FUNCT3_BLTU__   3'h6
`define FUNCT3_BGEU__   3'h7

module m_alu_b(w_in1, w_in2, w_funct3, r_rslt);
    input wire [31:0]  w_in1, w_in2;
    input wire  [2:0]  w_funct3;
    output reg         r_rslt;

    wire signed [31:0] w_sin1 = w_in1;
    wire signed [31:0] w_sin2 = w_in2;

    always@(*) begin
        case(w_funct3)
            `FUNCT3_BEQ___ : r_rslt = w_in1 == w_in2;
            `FUNCT3_BNE___ : r_rslt = w_in1 != w_in2;
            `FUNCT3_BLT___ : r_rslt = w_sin1 < w_sin2;
            `FUNCT3_BGE___ : r_rslt = w_sin1 >= w_sin2;
            `FUNCT3_BLTU__ : r_rslt = w_in1 < w_in2;
            `FUNCT3_BGEU__ : r_rslt = w_in1 >= w_in2;
            default        : r_rslt = 0;
        endcase
    end
endmodule

`define ACCESS_READ     0
`define ACCESS_WRITE    1
`define ACCESS_CODE     2

// local memory size and location
`define UC_TADDR        4'h1
`define D_UC_LM_BITS    14 // 2^14 = 16KB
`define D_UC_LM_SIZE    (1<<(`D_UC_LM_BITS))

// Simple RV32I CPU / microcontroller
//    addr 0x10000000-0x1fffffff is local memory, code must be here
//
module RV32I (
    input  wire         CLK,          // clock
`ifdef GW_IDE
    input  wire         clk_2x,       // 2x clock with +45 deg phase for emulating read-before-write memory
`endif
    input  wire         RST_X,        // nReset
    input  wire         w_stall,      // stall execution (wait for main memory)
    output wire [31:0]  w_mic_addr,   // CPU address,
    input  wire [31:0]  w_data,       //     data in,
    output wire [31:0]  w_mic_wdata,  //     data out,
    output wire         w_mic_mmuwe,  //     wr
    output wire  [2:0]  w_mic_ctrl,   // info size: 'funct3' field, to select b/h/w
    output wire  [1:0]  w_mic_req     // info TLB: type of access: 2 = IF, 1 = WR, 0 = RD, 3 = NONE
  );

    reg  [31:0] r_pc     = 0; // program counter (PC)
    reg   [1:0] r_state  = 0; // state, showing the execution stage
    reg  [31:0] r_ir     = 0; // instruction (IR) to be fetched
    reg   [6:0] r_opcode = 0; // a field of IR
    reg   [2:0] r_funct3 = 0; // a field of IR
    reg   [6:0] r_funct7 = 0; // a field of IR
    reg   [4:0] r_rd     = 0; // a field of IR
    reg  [31:0] r_imm    = 0; // immediate 
    reg  [31:0] r_rrs1   = 0; // the first  operand
    reg  [31:0] r_rrs2   = 0; // the second operand
    reg  [31:0] r_rslt   = 0; // the execution result

    reg  [31:0] r_dram_data = 0;
    
    always @(posedge CLK) begin
        r_state <= (!RST_X) ? 0 : (w_stall) ? r_state : (r_state==`MC_MA) ? `MC_IF : r_state+1;
    end

    wire [31:0] w_mem_rdata;
    wire        w_we = (r_opcode==`OPCODE_STORE___ && r_state==`MC_EX);

    wire [31:0] w_reg_d  = (r_opcode==`OPCODE_LOAD____) ? w_mem_rdata : r_rslt;
    wire        w_reg_we = (r_opcode==`OPCODE_LOAD____) ? 1 :
                           (r_opcode==`OPCODE_LUI_____) ? 1 :
                           (r_opcode==`OPCODE_AUIPC___) ? 1 :
                           (r_opcode==`OPCODE_JAL_____) ? 1 :
                           (r_opcode==`OPCODE_JALR____) ? 1 :
                           (r_opcode==`OPCODE_OP______) ? 1 :
                           (r_opcode==`OPCODE_OP_IMM__) ? 1 : 0;

    assign w_mic_req = (r_state==`MC_IF)                                ? `ACCESS_CODE  :
                       (r_state==`MC_EX && w_we)                        ? `ACCESS_WRITE :
                       (r_state==`MC_EX && r_opcode==`OPCODE_LOAD____)  ? `ACCESS_READ  : 3;

    /******************************************** OF  *********************************************/
    wire [31:0] w_ir;
    wire  [6:0] w_op      = w_ir[ 6: 0];
    wire  [4:0] w_rd      = w_ir[11: 7];
    wire  [4:0] w_rs1     = w_ir[19:15];
    wire  [4:0] w_rs2     = w_ir[24:20];
    wire  [2:0] w_funct3  = w_ir[14:12];
    wire  [6:0] w_funct7  = w_ir[31:25];
    wire [11:0] w_funct12 = w_ir[31:20];
    wire [31:0] w_imm;
    wire [31:0] w_rrs1, w_rrs2;

    m_imm_gen imm_gen0(w_ir, w_imm);

    wire w_reg_w = (w_reg_we && r_state==`MC_IF && !w_stall); // regfile write_enable
    
    m_regfile regs(CLK, w_rs1, w_rs2, w_rrs1, w_rrs2, w_reg_w, r_rd, w_reg_d);

    reg  [31:0] r_mic_addr  = 0;
    always @(posedge CLK) begin
        if(r_state == `MC_OF) begin
            r_ir       <= w_ir;
            r_opcode   <= w_op;
            r_rd       <= w_rd;
            r_funct3   <= w_funct3;
            r_funct7   <= w_funct7;
            r_imm      <= w_imm;
            r_rrs1     <= w_rrs1;
            r_rrs2     <= w_rrs2;
            r_mic_addr <= w_rrs1 + w_imm;
        end
    end

    /******************************************** EX  *********************************************/
    wire [31:0] w_alu_i_rslt; // integer ALU's result
    wire        w_alu_b_rslt; // branch resolution unit's result

    wire  [6:0] w_alu_fn7 = (r_opcode==`OPCODE_OP_IMM__) ? 
                            ((r_funct3==`FUNCT3_ADD___) ? 0 : r_funct7 & 7'h20) : r_funct7;

    wire [31:0] w_in2 = (r_opcode==`OPCODE_OP_IMM__) ? r_imm : r_rrs2;
    
    m_alu_i ALU_I (r_rrs1, w_in2, r_funct3, w_alu_fn7, w_alu_i_rslt);
    m_alu_b ALU_B (r_rrs1, w_in2, r_funct3, w_alu_b_rslt);

    reg  [31:0] r_jmp_pc    = 0;
    reg         r_tkn       = 0;

    always @(posedge CLK) begin
        if(r_state == `MC_EX) begin
           //$write("PC:%08x OPCODE=%7b, ir=%8x\n", r_pc, r_opcode, r_ir);
           case(r_opcode)  
               `OPCODE_LUI_____ : r_rslt <= r_imm;
               `OPCODE_AUIPC___ : r_rslt <= r_pc + r_imm;
               `OPCODE_JAL_____ : r_rslt <= r_pc + 4;
               `OPCODE_JALR____ : r_rslt <= r_pc + 4;
               `OPCODE_OP______ : r_rslt <= w_alu_i_rslt;
               `OPCODE_OP_IMM__ : r_rslt <= w_alu_i_rslt;
               `OPCODE_BRANCH__ : r_rslt <= 0;
               `OPCODE_LOAD____ : r_rslt <= 0;
               `OPCODE_STORE___ : r_rslt <= 0;
               `OPCODE_MISC_MEM : r_rslt <= 0;
               default : begin
                   r_rslt <= 0;
`ifdef __ICARUS__
                   $write("UNKNOWN OPCODE DETECT in Micro Controller!!\n");
                   $write("PC:%08x OPCODE=%7b, ir=%8x\n", r_pc, r_opcode, r_ir);
                   $write("Simulation Stopped...\n");
                   $finish();
`endif
               end
           endcase
            r_jmp_pc <= (r_opcode==`OPCODE_JALR____) ? r_rrs1+r_imm : r_pc+r_imm;
            r_tkn    <= (r_opcode==`OPCODE_JAL_____ || r_opcode==`OPCODE_JALR____) ? 1 :
                        (r_opcode==`OPCODE_BRANCH__) ? w_alu_b_rslt : 0;
        end
    end

    /******************************************** MA  *********************************************/
    always@(posedge CLK) begin
        r_dram_data <= w_data;
    end

    assign  w_mic_addr  = r_mic_addr;
    assign  w_mic_wdata = r_rrs2;
    assign  w_mic_ctrl  = r_funct3;

    wire [31:0] w_lcm_data; // data from local memory
    
    wire [31:0] w_mic_insn_addr = r_pc;
    wire [31:0] w_mic_insn_data;
    assign  w_ir = w_mic_insn_data;

    reg  [2:0] r_ctrl  = 3;
    reg  [1:0] r_addr2 = 0;

    wire [31:0] w_wdata_t, w_odata1, w_odata2;

    always@(posedge CLK) begin
        r_addr2 <= w_mic_addr;
        r_ctrl  <= w_mic_ctrl;
    end

    // Select Output DATA
    assign w_wdata_t =  (w_mic_ctrl[1:0]==0) ? {4{w_mic_wdata[ 7:0]}} :
                        (w_mic_ctrl[1:0]==1) ? {2{w_mic_wdata[15:0]}} :
                        w_mic_wdata;

    assign w_mic_insn_data  = w_odata1;
    
    wire [31:0] w_odata2_t   = w_odata2;
    wire [31:0] w_odata2_t2  = w_odata2_t >> {r_addr2[1:0], 3'b0};

    wire [31:0] w_odata2_lb  = {{24{w_odata2_t2[7]}}, w_odata2_t2[7:0]};
    wire [31:0] w_odata2_lbu = {24'h0, w_odata2_t2[7:0]};
    wire [31:0] w_odata2_lh  = {{16{w_odata2_t2[15]}}, w_odata2_t2[15:0]};
    wire [31:0] w_odata2_lhu = {16'h0, w_odata2_t2[15:0]};
    
    assign w_lcm_data = (r_ctrl[2:0]==3'b000) ? w_odata2_lb :
                        (r_ctrl[2:0]==3'b100) ? w_odata2_lbu:
                        (r_ctrl[2:0]==3'b001) ? w_odata2_lh :
                        (r_ctrl[2:0]==3'b101) ? w_odata2_lhu: w_odata2_t2;

    // Check Write Enable
    wire  [3:0] w_we_sb = (4'b0001 << w_mic_addr[1:0]);
    wire  [3:0] w_we_sh = (4'b0011 << {w_mic_addr[1], 1'b0});
    wire  [3:0] w_we_sw = 4'b1111;
    wire  [3:0] w_mic_lcmwe =   (!(w_we && w_mic_addr[31:28]==`UC_TADDR && !w_stall))   ? 0 :
                                (w_mic_ctrl[1:0]==2)                            ? w_we_sw :
                                (w_mic_ctrl[1:0]==1)                            ? w_we_sh : w_we_sb;

    // BRAM local memory
    m_lm_mc lm_mc
    (
        CLK, 
`ifdef GW_IDE
        clk_2x,
`endif
        w_mic_insn_addr[`D_UC_LM_BITS-1:2], w_odata1,
        w_mic_lcmwe, w_mic_addr[`D_UC_LM_BITS-1:2], w_odata2, w_wdata_t
    );
    
    assign w_mic_mmuwe  = w_we && (w_mic_addr[31:28]!=`UC_TADDR);
    assign w_mem_rdata  = (w_mic_addr[31:28]==`UC_TADDR) ? w_lcm_data : r_dram_data;

    // Update PC
    always @(posedge CLK) begin
        r_pc <= (!RST_X) ? {`UC_TADDR, 28'h0000000}
                         : (r_state!=`MC_MA || w_stall) ? r_pc : (r_tkn) ? r_jmp_pc : r_pc+4;
    end

endmodule

// Dual ported BRAM for controller local memory, preloaded with code
//

`ifndef GW_IDE
module m_lm_mc(
    input  wire                         CLK,

    input  wire [(`D_UC_LM_BITS-2)-1:0] w_addr1,    // instruction port
    output  reg                  [31:0] r_odata1,
    
    input  wire                   [3:0] w_we,       // data port
    input  wire [(`D_UC_LM_BITS-2)-1:0] w_addr2,
    output  reg                  [31:0] r_odata2,
    input  wire                  [31:0] w_idata
  );

    reg [31:0] mem[0:(`D_UC_LM_SIZE/4)-1];

    initial begin
        $readmemh("ucmem/mem.hex", mem);
    end

    always @(posedge CLK) begin
        r_odata1 <= mem[w_addr1];
    end

    always @(posedge CLK) begin
        if (w_we[0]) mem[w_addr2][ 7: 0] <= w_idata[ 7: 0];
        if (w_we[1]) mem[w_addr2][15: 8] <= w_idata[15: 8];
        if (w_we[2]) mem[w_addr2][23:16] <= w_idata[23:16];
        if (w_we[3]) mem[w_addr2][31:24] <= w_idata[31:24];
        r_odata2 <= mem[w_addr2];
    end

endmodule
`else //GW_IDE
module m_lm_mc
(
    input  wire                         CLK,
    input  wire                         clk_2x, //2x with phase offset ~45 deg

    input  wire [(`D_UC_LM_BITS-2)-1:0] w_addr1,    // instruction port
    output wire                  [31:0] r_odata1,
    
    input  wire                   [3:0] w_we,       // data port
    input  wire [(`D_UC_LM_BITS-2)-1:0] w_addr2,
    output  reg                  [31:0] r_odata2,
    input  wire                  [31:0] w_idata
);

    module gowin_dpb
    (
        input wire clka,          //gw5a-25 BRAM does not support read-before-write in dualport mode + is bad at inferring
        input wire clkb,          //so i tried my best to make it infer and here it is: 
                                  //bypass read port A at 1x clock
        input wire [31:0] dinb,   //and bypass read and write-through write port B at 45 deg delayed 2x clock, first read, then write
                                  //all of this just to be able to use $readmemh, instead of IP core creation tool
        output wire [31:0] douta,
        output wire [31:0] doutb,

        input wire [(`D_UC_LM_BITS-2)-1:0] ada,
        input wire [(`D_UC_LM_BITS-2)-1:0] adb,

        input wire wreb,
        input wire [3:0] byte_enb
    );

        reg [31:0] mem[0:(`D_UC_LM_SIZE/4)-1];

        initial begin
            $readmemh("ucmem/mem.hex", mem);
        end

        reg [(`D_UC_LM_BITS-2)-1:0] ada_reg, adb_reg;

        always @(posedge clka)
            ada_reg <= ada;

        assign douta = mem[ada_reg];

        always @(posedge clkb)
            adb_reg <= adb;

        assign doutb = mem[adb_reg];

        always @(posedge clkb)
        begin
            if (wreb)
            begin
                if (byte_enb[0]) mem[adb][ 7: 0] <= dinb[ 7: 0];
                if (byte_enb[1]) mem[adb][15: 8] <= dinb[15: 8];
                if (byte_enb[2]) mem[adb][23:16] <= dinb[23:16];
                if (byte_enb[3]) mem[adb][31:24] <= dinb[31:24];
            end
        end

    endmodule
    
    reg [31:0] din_reg, doutb_reg;
    reg [(`D_UC_LM_BITS-2)-1:0] w_addr1_reg;
    reg [3:0] byte_en_reg;

    gowin_dpb dpb
    (        
        .clka(CLK),
        .douta(r_odata1),
        .ada(w_addr1),

        .clkb(clk_2x),
        .dinb(din_reg),
        .doutb(doutb_reg),
        .adb(w_addr1_reg),
        .wreb(~CLK),
        .byte_enb(byte_en_reg)
    );

    always @(posedge CLK) begin
        din_reg <= w_idata;
        w_addr1_reg <= w_addr1;
        byte_en_reg <= w_we;
    end

    always @(negedge CLK) begin
        r_odata2 <= doutb_reg;
    end

endmodule
`endif