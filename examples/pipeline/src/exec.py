
import conduit

reg = conduit.registrars['pipeline']

fetch = reg.lookup('fetch')
stack = conduit.Stack()
def exec(instr):
    instr.exec(stack)
    conduit.sched(1, fetch.call);
reg.lookup('exec').hook(exec)
