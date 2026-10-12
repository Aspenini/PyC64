import first
import second as strings
from exports import Number, owned as shared
def edit(value: Number):
    value.value += 1
value: Number = Number(1000)
text = strings.Item()
edit(value)
edit(shared)
print(value.get(), text.get(), first.owned.value, shared.get())
#> 1001 OTHER 501 501
