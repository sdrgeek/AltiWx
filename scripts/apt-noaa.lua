logger:info("Processing NOAA APT data...")

output_file = filename .. ".png"

outflag = ""
if (northbound) then
    outflag = "-N"
else if (southbound)
    outflag = "-S"
end

command = "wxtoimg -q -A " .. outflag .. " -e HVCT '" .. input_file .. "' '" .. output_file .. "'"

logger:debug(command)
local cmd_output = os.execute(command)
if (not cmd_output == 0) then logger:error("WxToImg command failed!") end

logger:info("Done processing NOAA APT data!");
