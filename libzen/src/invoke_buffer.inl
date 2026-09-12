/*
** invoke_buffer.inl — Buffer method dispatch for OP_INVOKE.
** Minimal API — raw speed via buf[i] / buf[i]=x is the primary interface.
*/

ObjBuffer *buf = as_buffer(receiver);

#define BUFFER_METHOD(lit) (method->length == (int)(sizeof(lit) - 1) && memcmp(mname, lit, sizeof(lit) - 1) == 0)

do
{
if (BUFFER_METHOD("len"))
{
    R[base] = val_int(buf->count);
    break;
}
if (BUFFER_METHOD("fill"))
{
    if (arg_count != 1)
    {
        RT_ERROR("fill() expects 1 argument");
    }
    double v = 0;
    if (is_int(args[0]))
        v = (double)args[0].as.integer;
    else if (is_float(args[0]))
        v = args[0].as.number;
    else
    {
        RT_ERROR("fill() expects a number");
    }
    buffer_fill(buf, v);
    R[base] = receiver;
    break;
}
if (BUFFER_METHOD("byte_len"))
{
    R[base] = val_int(buf->count * buffer_elem_size[buf->btype]);
    break;
}
{
    RT_ERROR("buffer has no method '%s'", mname);
}
} while (0);

#undef BUFFER_METHOD
