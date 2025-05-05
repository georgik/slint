// SPDX-License-Identifier: MIT
//! Board support for ESP32-S3-LCD-EV-Board using the same init sequence as Conway's Game of Life example.

extern crate alloc;
use esp_hal::clock::CpuClock::_240MHz;
use esp_hal::delay::Delay;
use esp_hal::dma::{CHUNK_SIZE, DmaDescriptor};
use esp_hal::i2c::master::{I2c, Config as I2cConfig, Error};
use esp_hal::lcd_cam::{
    LcdCam,
    lcd::{
        ClockMode, Phase, Polarity,
        dpi::{Config as DpiConfig, Dpi, Format, FrameTiming},
    },
};
use esp_hal::{
    Blocking,
    Config as HalConfig,
    gpio::{Level, Output, OutputConfig},
    init,
    time::Rate,
};
use log::{info, error};
use esp_println::logger::init_logger_from_env;

// === Display constants ===
const LCD_H_RES: u16 = 480;
const LCD_V_RES: u16 = 480;
const FRAME_BYTES: usize = (LCD_H_RES as usize * LCD_V_RES as usize) * 2;
const NUM_DMA_DESC: usize = (FRAME_BYTES + CHUNK_SIZE - 1) / CHUNK_SIZE;

// Place DMA descriptors in DMA-capable RAM
#[link_section = ".dma"]
static mut TX_DESCRIPTORS: [DmaDescriptor; NUM_DMA_DESC] = [DmaDescriptor::EMPTY; NUM_DMA_DESC];

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    error!("Panic: {}", _info);
    loop {}
}

// --- I2C expander (TCA9554) ---
struct Tca9554 {
    i2c: I2c<'static, esp_hal::Blocking>,
    address: u8,
}

impl Tca9554 {
    pub fn new(i2c: I2c<'static, esp_hal::Blocking>) -> Self {
        Self { i2c, address: 0x20 }
    }
    pub fn write_direction_reg(&mut self, value: u8) -> Result<(), Error> {
        self.i2c.write(self.address, &[0x03, value])
    }
    pub fn write_output_reg(&mut self, value: u8) -> Result<(), Error> {
        self.i2c.write(self.address, &[0x01, value])
    }
}

// Initialization commands matching the Conway example
#[derive(Copy, Clone, Debug)]
enum InitCmd {
    Cmd(u8, &'static [u8]),
    Delay(u8),
}

const INIT_CMDS: &[InitCmd] = &[
    InitCmd::Cmd(0xf0, &[0x55, 0xaa, 0x52, 0x08, 0x00]),
    InitCmd::Cmd(0xf6, &[0x5a, 0x87]),
    InitCmd::Cmd(0xc1, &[0x3f]),
    InitCmd::Cmd(0xc2, &[0x0e]),
    InitCmd::Cmd(0xc6, &[0xf8]),
    InitCmd::Cmd(0xc9, &[0x10]),
    InitCmd::Cmd(0xcd, &[0x25]),
    InitCmd::Cmd(0xf8, &[0x8a]),
    InitCmd::Cmd(0xac, &[0x45]),
    InitCmd::Cmd(0xa0, &[0xdd]),
    InitCmd::Cmd(0xa7, &[0x47]),
    InitCmd::Cmd(0xfa, &[0x00, 0x00, 0x00, 0x04]),
    InitCmd::Cmd(0x86, &[0x99, 0xa3, 0xa3, 0x51]),
    InitCmd::Cmd(0xa3, &[0xee]),
    InitCmd::Cmd(0xfd, &[0x3c, 0x3]),
    InitCmd::Cmd(0x71, &[0x48]),
    InitCmd::Cmd(0x72, &[0x48]),
    InitCmd::Cmd(0x73, &[0x00, 0x44]),
    InitCmd::Cmd(0x97, &[0xee]),
    InitCmd::Cmd(0x83, &[0x93]),
    InitCmd::Cmd(0x9a, &[0x72]),
    InitCmd::Cmd(0x9b, &[0x5a]),
    InitCmd::Cmd(0x82, &[0x2c, 0x2c]),
    InitCmd::Cmd(0xB1, &[0x10]),
    InitCmd::Cmd(
        0x6d,
        &[
            0x00, 0x1f, 0x19, 0x1a, 0x10, 0x0e, 0x0c, 0x0a, 0x02, 0x07, 0x1e, 0x1e, 0x1e, 0x1e,
            0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x08, 0x01, 0x09, 0x0b, 0x0d, 0x0f,
            0x1a, 0x19, 0x1f, 0x00,
        ],
    ),
    InitCmd::Cmd(
        0x64,
        &[
            0x38, 0x05, 0x01, 0xdb, 0x03, 0x03, 0x38, 0x04, 0x01, 0xdc, 0x03, 0x03, 0x7a, 0x7a,
            0x7a, 0x7a,
        ],
    ),
    InitCmd::Cmd(
        0x65,
        &[
            0x38, 0x03, 0x01, 0xdd, 0x03, 0x03, 0x38, 0x02, 0x01, 0xde, 0x03, 0x03, 0x7a, 0x7a,
            0x7a, 0x7a,
        ],
    ),
    InitCmd::Cmd(
        0x66,
        &[
            0x38, 0x01, 0x01, 0xdf, 0x03, 0x03, 0x38, 0x00, 0x01, 0xe0, 0x03, 0x03, 0x7a, 0x7a,
            0x7a, 0x7a,
        ],
    ),
    InitCmd::Cmd(
        0x67,
        &[
            0x30, 0x01, 0x01, 0xe1, 0x03, 0x03, 0x30, 0x02, 0x01, 0xe2, 0x03, 0x03, 0x7a, 0x7a,
            0x7a, 0x7a,
        ],
    ),
    InitCmd::Cmd(
        0x68,
        &[
            0x00, 0x08, 0x15, 0x08, 0x15, 0x7a, 0x7a, 0x08, 0x15, 0x08, 0x15, 0x7a, 0x7a,
        ],
    ),
    InitCmd::Cmd(0x60, &[0x38, 0x08, 0x7a, 0x7a, 0x38, 0x09, 0x7a, 0x7a]),
    InitCmd::Cmd(0x63, &[0x31, 0xe4, 0x7a, 0x7a, 0x31, 0xe5, 0x7a, 0x7a]),
    InitCmd::Cmd(0x69, &[0x04, 0x22, 0x14, 0x22, 0x14, 0x22, 0x08]),
    InitCmd::Cmd(0x6b, &[0x07]),
    InitCmd::Cmd(0x7a, &[0x08, 0x13]),
    InitCmd::Cmd(0x7b, &[0x08, 0x13]),
    InitCmd::Cmd(
        0xd1,
        &[
            0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35,
            0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7,
            0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5,
            0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff,
        ],
    ),
    InitCmd::Cmd(
        0xd2,
        &[
            0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35,
            0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7,
            0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5,
            0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff,
        ],
    ),
    InitCmd::Cmd(
        0xd3,
        &[
            0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35,
            0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7,
            0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5,
            0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff,
        ],
    ),
    InitCmd::Cmd(
        0xd4,
        &[
            0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35,
            0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7,
            0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5,
            0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff,
        ],
    ),
    InitCmd::Cmd(
        0xd5,
        &[
            0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35,
            0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7,
            0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5,
            0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff,
        ],
    ),
    InitCmd::Cmd(
        0xd6,
        &[
            0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35,
            0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7,
            0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5,
            0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff,
        ],
    ),
    InitCmd::Cmd(0x36, &[0x00]),
    InitCmd::Cmd(0x2A, &[0x00, 0x00, 0x01, 0xDF]), // 0 to 479 (0x1DF)
    // Set full row address range
    InitCmd::Cmd(0x2B, &[0x00, 0x00, 0x01, 0xDF]), // 0 to 479 (0x1DF)
    InitCmd::Cmd(0x3A, &[0x66]),
    InitCmd::Cmd(0x11, &[]),
    InitCmd::Delay(120),
    InitCmd::Cmd(0x29, &[]),
    InitCmd::Delay(20),
];


/// Initializes the display and expander; returns the configured DPI interface and the TX_DESCRIPTOR slice.
pub fn init_display() -> (Dpi<'static, Blocking>, &'static mut [DmaDescriptor]) {
    init_logger_from_env();

    info!("Board init: starting ESP32-S3 LCD EV board setup");
    // Initialize peripherals and PSRAM allocator
    let mut peripherals = init(HalConfig::default().with_cpu_clock(_240MHz));
    esp_alloc::psram_allocator!(peripherals.PSRAM, esp_hal::psram);
    info!("Peripherals initialized, PSRAM allocator set up");

    // Setup I2C for expander
    let i2c = I2c::new(
        peripherals.I2C0,
        I2cConfig::default().with_frequency(Rate::from_khz(400)),
    )
        .unwrap()
        .with_sda(peripherals.GPIO47)
        .with_scl(peripherals.GPIO48);
    let mut expander = Tca9554::new(i2c);
    expander.write_output_reg(0b1111_0011).unwrap();
    expander.write_direction_reg(0b1111_0001).unwrap();
    info!("I2C expander configured (power/backlight)");

    let delay = Delay::new();

    // Bitbang write_byte as in main.rs
    let mut write_byte = |b: u8, is_cmd: bool| {
        const SCS: u8 = 0b0000_0010;
        const SCL: u8 = 0b0000_0100;
        const SDA: u8 = 0b0000_1000;
        let mut out = 0b1111_0001 & !SCS;
        expander.write_output_reg(out).unwrap();
        for bit in core::iter::once(!is_cmd).chain((0..8).map(|i| (b >> i) & 1 != 0).rev()) {
            if bit { out |= SDA } else { out &= !SDA }
            expander.write_output_reg(out).unwrap();
            out &= !SCL;
            expander.write_output_reg(out).unwrap();
            out |= SCL;
            expander.write_output_reg(out).unwrap();
        }
        out &= !SCL; expander.write_output_reg(out).unwrap();
        out &= !SDA; expander.write_output_reg(out).unwrap();
        out |= SCS; expander.write_output_reg(out).unwrap();
    };

    // Keep VSYNC high during init
    let vsync_guard = Output::new(
        peripherals.GPIO3.reborrow(),
        Level::High,
        OutputConfig::default(),
    );

    info!("Sending display controller init sequence");
    // Run init sequence
    for &cmd in INIT_CMDS {
        info!("InitCmd: {:?}", cmd);
        match cmd {
            InitCmd::Cmd(code, args) => {
                write_byte(code, true);
                for &arg in args { write_byte(arg, false); }
            }
            InitCmd::Delay(ms) => delay.delay_millis(ms as _),
        }
    }
    drop(vsync_guard);
    info!("Display init sequence complete");

    // Setup DMA descriptors
    let tx_channel = peripherals.DMA_CH2;

    // Init LcdCam and DPI configuration
    let lcd_cam = LcdCam::new(peripherals.LCD_CAM);
    let dpi_config = DpiConfig::default()
        .with_clock_mode(ClockMode { polarity: Polarity::IdleLow, phase: Phase::ShiftLow })
        .with_frequency(Rate::from_mhz(10))
        .with_format(Format { enable_2byte_mode: true, ..Default::default() })
        .with_timing(FrameTiming {
            horizontal_active_width: LCD_H_RES as usize,
            vertical_active_height: LCD_V_RES as usize,
            horizontal_total_width: 600,
            horizontal_blank_front_porch: 80,
            vertical_total_height: 600,
            vertical_blank_front_porch: 80,
            hsync_width: 10,
            vsync_width: 4,
            hsync_position: 10,
        })
        .with_vsync_idle_level(Level::High)
        .with_hsync_idle_level(Level::High)
        .with_de_idle_level(Level::Low)
        .with_disable_black_region(false);

    // Build the DPI interface
    let dpi = Dpi::new(lcd_cam.lcd, tx_channel, dpi_config)
        .unwrap()
        .with_vsync(peripherals.GPIO3)
        .with_hsync(peripherals.GPIO46)
        .with_de(peripherals.GPIO17)
        .with_pclk(peripherals.GPIO9)
        // Blue
        .with_data0(peripherals.GPIO10)
        .with_data1(peripherals.GPIO11)
        .with_data2(peripherals.GPIO12)
        .with_data3(peripherals.GPIO13)
        .with_data4(peripherals.GPIO14)
        // Green
        .with_data5(peripherals.GPIO21)
        .with_data6(peripherals.GPIO8)
        .with_data7(peripherals.GPIO18)
        .with_data8(peripherals.GPIO45)
        .with_data9(peripherals.GPIO38)
        .with_data10(peripherals.GPIO39)
        // Red
        .with_data11(peripherals.GPIO40)
        .with_data12(peripherals.GPIO41)
        .with_data13(peripherals.GPIO42)
        .with_data14(peripherals.GPIO2)
        .with_data15(peripherals.GPIO1);
    info!("DPI interface initialized successfully");

    (dpi, unsafe { &mut TX_DESCRIPTORS })
}
