-- neorv32_soc_icap_sr.vhd - NEORV32 SoC for EHW-3.4 internal-ICAPE2 spare-route eval.
--
-- XBUS memory map seen by NEORV32:
--   0xF1000xxx : MBOX status register (write) -> mbox_o (PS reads over AXI-GPIO)
--   0xF3000xxx : xbus_icap controller (custom XBUS->ICAPE2)
--   0xF4000xxx : baked spare-route island target (drive input/read output)
--   0xF5000xxx-0xF5001xxx : frame payload BRAM bank -> staged by PS via AXI-Lite
--   others     : default ACK, reads 0
--
-- This design intentionally has no PS-HWICAP instance. The board flow must not use
-- PS-HWICAP readreg/writeseq commands against it; ICAP writes are internal only.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library neorv32;
use neorv32.neorv32_package.all;

entity neorv32_soc_icap_sr is
  port (
    clk_i        : in  std_ulogic;
    rstn_i       : in  std_ulogic;
    uart0_txd_o  : out std_ulogic;
    uart0_rxd_i  : in  std_ulogic;
    mbox_o       : out std_ulogic_vector(31 downto 0);
    mbox_valid_o : out std_ulogic;
    lut_o        : out std_ulogic_vector(31 downto 0);
    s_axi_aclk    : in  std_logic;
    s_axi_aresetn : in  std_logic;
    s_axi_awaddr  : in  std_logic_vector(12 downto 0);
    s_axi_awvalid : in  std_logic;
    s_axi_awready : out std_logic;
    s_axi_wdata   : in  std_logic_vector(31 downto 0);
    s_axi_wstrb   : in  std_logic_vector(3 downto 0);
    s_axi_wvalid  : in  std_logic;
    s_axi_wready  : out std_logic;
    s_axi_bresp   : out std_logic_vector(1 downto 0);
    s_axi_bvalid  : out std_logic;
    s_axi_bready  : in  std_logic;
    s_axi_araddr  : in  std_logic_vector(12 downto 0);
    s_axi_arvalid : in  std_logic;
    s_axi_arready : out std_logic;
    s_axi_rdata   : out std_logic_vector(31 downto 0);
    s_axi_rresp   : out std_logic_vector(1 downto 0);
    s_axi_rvalid  : out std_logic;
    s_axi_rready  : in  std_logic
  );
end entity neorv32_soc_icap_sr;

architecture rtl of neorv32_soc_icap_sr is
  signal rstn_int : std_ulogic;
  signal por_cnt  : std_ulogic_vector(3 downto 0) := (others => '0');

  signal xbus_adr   : std_ulogic_vector(31 downto 0);
  signal xbus_dat_o : std_ulogic_vector(31 downto 0);
  signal xbus_dat_i : std_ulogic_vector(31 downto 0);
  signal xbus_we    : std_ulogic;
  signal xbus_sel   : std_ulogic_vector(3 downto 0);
  signal xbus_stb   : std_ulogic;
  signal xbus_cyc   : std_ulogic;
  signal xbus_ack   : std_ulogic;
  signal xbus_err   : std_ulogic;

  signal mbox_selected : std_ulogic;
  signal icap_selected : std_ulogic;
  signal sr_selected   : std_ulogic;
  signal fb_selected   : std_ulogic;
  signal def_selected  : std_ulogic;

  signal icap_dat_r : std_ulogic_vector(31 downto 0);
  signal icap_ack   : std_ulogic;
  signal icap_err   : std_ulogic;
  signal icap_stb   : std_ulogic;
  signal sr_dat_r   : std_ulogic_vector(31 downto 0);
  signal sr_ack     : std_ulogic;
  signal sr_err     : std_ulogic;
  signal sr_stb     : std_ulogic;
  signal sr_q       : std_ulogic_vector(31 downto 0);

  signal mbox_reg  : std_ulogic_vector(31 downto 0) := (others => '0');
  signal mbox_vld  : std_ulogic := '0';
  signal mbox_ack  : std_ulogic := '0';
  signal def_ack   : std_ulogic := '0';
  signal fb_ack    : std_ulogic := '0';
  signal fb_rddata : std_logic_vector(31 downto 0);

  component axil_framebuf is
    port (
      s_axi_aclk    : in  std_logic;
      s_axi_aresetn : in  std_logic;
      s_axi_awaddr  : in  std_logic_vector(12 downto 0);
      s_axi_awvalid : in  std_logic;
      s_axi_awready : out std_logic;
      s_axi_wdata   : in  std_logic_vector(31 downto 0);
      s_axi_wstrb   : in  std_logic_vector(3 downto 0);
      s_axi_wvalid  : in  std_logic;
      s_axi_wready  : out std_logic;
      s_axi_bresp   : out std_logic_vector(1 downto 0);
      s_axi_bvalid  : out std_logic;
      s_axi_bready  : in  std_logic;
      s_axi_araddr  : in  std_logic_vector(12 downto 0);
      s_axi_arvalid : in  std_logic;
      s_axi_arready : out std_logic;
      s_axi_rdata   : out std_logic_vector(31 downto 0);
      s_axi_rresp   : out std_logic_vector(1 downto 0);
      s_axi_rvalid  : out std_logic;
      s_axi_rready  : in  std_logic;
      rd_addr       : in  std_logic_vector(10 downto 0);
      rd_data       : out std_logic_vector(31 downto 0)
    );
  end component;

  component xbus_icap is
    port (
      clk        : in  std_ulogic;
      rst_n      : in  std_ulogic;
      xbus_adr   : in  std_ulogic_vector(31 downto 0);
      xbus_dat_w : in  std_ulogic_vector(31 downto 0);
      xbus_sel   : in  std_ulogic_vector(3 downto 0);
      xbus_we    : in  std_ulogic;
      xbus_stb   : in  std_ulogic;
      xbus_cyc   : in  std_ulogic;
      xbus_dat_r : out std_ulogic_vector(31 downto 0);
      xbus_ack   : out std_ulogic;
      xbus_err   : out std_ulogic
    );
  end component;

  component ehw34_spare_route_target is
    port (
      clk        : in  std_ulogic;
      rst_n      : in  std_ulogic;
      xbus_adr   : in  std_ulogic_vector(31 downto 0);
      xbus_dat_w : in  std_ulogic_vector(31 downto 0);
      xbus_sel   : in  std_ulogic_vector(3 downto 0);
      xbus_we    : in  std_ulogic;
      xbus_stb   : in  std_ulogic;
      xbus_cyc   : in  std_ulogic;
      xbus_dat_r : out std_ulogic_vector(31 downto 0);
      xbus_ack   : out std_ulogic;
      xbus_err   : out std_ulogic;
      q          : out std_ulogic_vector(31 downto 0)
    );
  end component;

begin
  por: process(clk_i)
  begin
    if rising_edge(clk_i) then
      if rstn_i = '0' then
        por_cnt <= (others => '0');
      elsif por_cnt /= x"F" then
        por_cnt <= std_ulogic_vector(unsigned(por_cnt) + 1);
      end if;
    end if;
  end process;
  rstn_int <= '1' when (por_cnt = x"F") else '0';

  mbox_selected <= '1' when xbus_adr(31 downto 12) = x"F1000" else '0';
  icap_selected <= '1' when xbus_adr(31 downto 12) = x"F3000" else '0';
  sr_selected   <= '1' when xbus_adr(31 downto 12) = x"F4000" else '0';
  fb_selected   <= '1' when (xbus_adr(31 downto 12) = x"F5000" or
                             xbus_adr(31 downto 12) = x"F5001") else '0';
  def_selected  <= '1' when (mbox_selected = '0' and icap_selected = '0' and
                             sr_selected = '0' and fb_selected = '0') else '0';

  icap_stb <= xbus_stb when icap_selected = '1' else '0';
  sr_stb   <= xbus_stb when sr_selected = '1' else '0';

  mbox: process(clk_i)
  begin
    if rising_edge(clk_i) then
      if rstn_int = '0' then
        mbox_reg <= (others => '0'); mbox_vld <= '0'; mbox_ack <= '0';
      else
        mbox_ack <= xbus_cyc and xbus_stb and mbox_selected;
        if (xbus_cyc and xbus_stb and mbox_selected and xbus_we) = '1' then
          mbox_reg <= xbus_dat_o; mbox_vld <= '1';
        end if;
      end if;
    end if;
  end process;

  defslv: process(clk_i)
  begin
    if rising_edge(clk_i) then
      def_ack <= xbus_cyc and xbus_stb and def_selected;
    end if;
  end process;

  fbproc: process(clk_i)
  begin
    if rising_edge(clk_i) then
      fb_ack <= xbus_cyc and xbus_stb and fb_selected;
    end if;
  end process;

  xbus_dat_i <= icap_dat_r                   when icap_ack = '1' else
                sr_dat_r                     when sr_ack    = '1' else
                mbox_reg                     when mbox_ack  = '1' else
                std_ulogic_vector(fb_rddata) when fb_ack    = '1' else
                (others => '0');
  xbus_ack   <= icap_ack or sr_ack or mbox_ack or fb_ack or def_ack;
  xbus_err   <= icap_err or sr_err;

  mbox_o       <= mbox_reg;
  mbox_valid_o <= mbox_vld;
  lut_o        <= sr_q;

  neorv32_inst: neorv32_top
  generic map (
    CLOCK_FREQUENCY  => 50_000_000, BOOT_MODE_SELECT => 2,
    RISCV_ISA_C => true, RISCV_ISA_M => true, RISCV_ISA_U => true,
    RISCV_ISA_Zaamo => true, RISCV_ISA_Zalrsc => true, RISCV_ISA_Zicntr => true,
    IMEM_EN => true, IMEM_SIZE => 32*1024, DMEM_EN => true, DMEM_SIZE => 16*1024,
    XBUS_EN => true, XBUS_TIMEOUT => 4096, XBUS_REGSTAGE_EN => false,
    ICACHE_EN => true, CACHE_BLOCK_SIZE => 64, DCACHE_EN => false,
    IO_GPIO_NUM => 4, IO_CLINT_EN => true, IO_UART0_EN => true,
    IO_UART0_RX_FIFO => 4, IO_UART0_TX_FIFO => 4, IO_SPI_EN => false,
    OCD_EN => false, DUAL_CORE_EN => false
  )
  port map (
    clk_i => clk_i, rstn_i => rstn_int,
    xbus_adr_o => xbus_adr, xbus_dat_o => xbus_dat_o, xbus_cti_o => open,
    xbus_tag_o => open, xbus_dat_i => xbus_dat_i, xbus_we_o => xbus_we,
    xbus_sel_o => xbus_sel, xbus_stb_o => xbus_stb, xbus_cyc_o => xbus_cyc,
    xbus_ack_i => xbus_ack, xbus_err_i => xbus_err,
    gpio_o => open, uart0_txd_o => uart0_txd_o, uart0_rxd_i => uart0_rxd_i
  );

  icap_inst: xbus_icap
  port map (
    clk => clk_i, rst_n => rstn_int, xbus_adr => xbus_adr, xbus_dat_w => xbus_dat_o,
    xbus_sel => xbus_sel, xbus_we => xbus_we, xbus_stb => icap_stb, xbus_cyc => xbus_cyc,
    xbus_dat_r => icap_dat_r, xbus_ack => icap_ack, xbus_err => icap_err
  );

  sr_inst: ehw34_spare_route_target
  port map (
    clk => clk_i, rst_n => rstn_int, xbus_adr => xbus_adr, xbus_dat_w => xbus_dat_o,
    xbus_sel => xbus_sel, xbus_we => xbus_we, xbus_stb => sr_stb, xbus_cyc => xbus_cyc,
    xbus_dat_r => sr_dat_r, xbus_ack => sr_ack, xbus_err => sr_err, q => sr_q
  );

  framebuf_inst: axil_framebuf
  port map (
    s_axi_aclk => s_axi_aclk, s_axi_aresetn => s_axi_aresetn,
    s_axi_awaddr => s_axi_awaddr, s_axi_awvalid => s_axi_awvalid, s_axi_awready => s_axi_awready,
    s_axi_wdata => s_axi_wdata, s_axi_wstrb => s_axi_wstrb, s_axi_wvalid => s_axi_wvalid,
    s_axi_wready => s_axi_wready, s_axi_bresp => s_axi_bresp, s_axi_bvalid => s_axi_bvalid,
    s_axi_bready => s_axi_bready, s_axi_araddr => s_axi_araddr, s_axi_arvalid => s_axi_arvalid,
    s_axi_arready => s_axi_arready, s_axi_rdata => s_axi_rdata, s_axi_rresp => s_axi_rresp,
    s_axi_rvalid => s_axi_rvalid, s_axi_rready => s_axi_rready,
    rd_addr => std_logic_vector(xbus_adr(12 downto 2)), rd_data => fb_rddata
  );
end architecture rtl;
