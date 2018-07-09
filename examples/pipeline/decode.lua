
conduit.registrars.pipeline.hook('decode', function(line)
	local parts = {}
        for i in line:gmatch('%S+') do
		parts[#parts + 1] = i
	end
	if #parts ~= 2 then
		conduit.registrars.pipeline.call('fetch')
		return
	end
	local instr = {
		op = parts[1],
		arg = parts[2]
	}
	conduit.registrars.pipeline.call('exec', instr)
end, 'lua')

