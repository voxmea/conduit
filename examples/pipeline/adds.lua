
local num_adds = 0
conduit.registrars.pipeline.hook('exec', function(instr)
    if instr.name == 'add' then
        num_adds = num_adds + 1
    end
end, 'lua')

conduit.registrars.pipeline.hook('stats', function(...)
    print('num_adds', num_adds)
end, 'lua')
