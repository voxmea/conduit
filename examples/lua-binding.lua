
local reg = conduit.registrars['test-reg']

reg.hook('foo', function(left, right)
    printf("lua - i=%d s=%s d=%f\n", left.i, left.s, left.d)
    printf("lua - i=%d s=%s d=%f\n", right.i, right.s, right.d)
end, 'lua')

reg.call('foo', {i=1, s='asdf', d=2.0}, {i=2, s='qewr', d=3.0})

reg.hook('bar', function(...)
    printf('lua got bar\n')
end, 'lua')

reg.call('bar')
