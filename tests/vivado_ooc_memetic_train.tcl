read_verilog rtl/pe.v
read_verilog rtl/systolic_array_4x4.v
read_verilog rtl/tpu_accel.v
read_verilog rtl/wb_tpu_accel.v
read_verilog rtl/memetic_train_unit.v
read_verilog rtl/dfx/tpu_rp_rm_memetic_train.v
synth_design -top tpu_rp -part xc7z010clg400-1
report_utilization -hierarchical
