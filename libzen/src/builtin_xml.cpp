/* =========================================================
** builtin_xml.cpp — "xml" module for Zen
**
** Thin wrapper around TinyXML-2 (leethomason/tinyxml2).
** Two classes:
**   XMLDoc  — document (load/parse/save/print)
**   XMLElem — element node (name, text, attrs, children)
**
** Usage:
**   import xml;
**
**   // parse from string
**   var doc = xml.parse("<root><item id='1'>hello</item></root>");
**   var root = doc.root();
**   print(root.name());                   // root
**   var item = root.child("item");
**   print(item.text());                   // hello
**   print(item.attr("id"));              // 1
**
**   // iterate children
**   var ch = root.firstChild();
**   while (ch != nil) {
**       print(ch.name());
**       ch = ch.next();
**   }
**
**   // build document
**   var doc2 = xml.create();
**   var r = doc2.createElement("root");
**   doc2.setRoot(r);
**   var e = doc2.createElement("entry");
**   e.setAttr("key", "name");
**   e.setText("zen");
**   r.append(e);
**   print(doc2.toString());
**
**   // file I/O
**   doc.loadFile("data.xml");
**   doc.saveFile("out.xml");
** ========================================================= */

#include "module.h"
#include "vm.h"
#include "memory.h"
#include "tinyxml2.h"
#include <cstring>
#include <cstdio>

namespace zen
{

using namespace tinyxml2;

/* =========================================================
** Forward declarations
** ========================================================= */

static ObjClass *g_elem_class = nullptr;

/* =========================================================
** Helper — wrap XMLElement* in a Zen instance
** returns val_nil() if elem == nullptr
** ========================================================= */

struct ElemData
{
    XMLElement *elem = nullptr;
    /* NOTE: ElemData does NOT own the element — the XMLDocument does.
       We keep a back-pointer to the doc instance to prevent GC from
       collecting it while elements are alive. */
    ObjInstance *doc_inst = nullptr;
};

static Value wrap_elem(VM *vm, XMLElement *elem, ObjInstance *doc_inst)
{
    if (!elem) return val_nil();
    Value inst = vm->make_instance(g_elem_class);
    if (is_nil(inst)) return val_nil();
    ElemData *ed = zen_instance_data<ElemData>(inst);
    ed->elem = elem;
    ed->doc_inst = doc_inst;
    return inst;
}

static ElemData *elem_data(Value v)
{
    return zen_instance_data<ElemData>(v);
}

/* =========================================================
** XMLDoc class
** ========================================================= */

struct DocData
{
    XMLDocument *doc = nullptr;

    DocData()  { doc = new XMLDocument(); }
    ~DocData() { delete doc; }
};

static DocData *doc_data(Value v)
{
    return zen_instance_data<DocData>(v);
}

static void *xml_doc_ctor(VM *, int, Value *)
{
    return new DocData();
}

static void xml_doc_dtor(VM *, void *p)
{
    delete (DocData *)p;
}

/* XMLDoc.parse(str) → bool */
static int xml_doc_parse(VM *vm, Value *args, int nargs)
{
    DocData *dd = doc_data(args[0]);
    if (nargs < 2 || !is_string(args[1]))
    {
        vm->runtime_error("XMLDoc.parse() expects (xmlString)");
        return -1;
    }
    XMLError err = dd->doc->Parse(as_cstring(args[1]));
    args[0] = val_bool(err == XML_SUCCESS);
    return 1;
}

/* XMLDoc.loadFile(path) → bool */
static int xml_doc_load_file(VM *vm, Value *args, int nargs)
{
    DocData *dd = doc_data(args[0]);
    if (nargs < 2 || !is_string(args[1]))
    {
        vm->runtime_error("XMLDoc.loadFile() expects (path)");
        return -1;
    }
    XMLError err = dd->doc->LoadFile(as_cstring(args[1]));
    args[0] = val_bool(err == XML_SUCCESS);
    return 1;
}

/* XMLDoc.saveFile(path) → bool */
static int xml_doc_save_file(VM *vm, Value *args, int nargs)
{
    DocData *dd = doc_data(args[0]);
    if (nargs < 2 || !is_string(args[1]))
    {
        vm->runtime_error("XMLDoc.saveFile() expects (path)");
        return -1;
    }
    XMLError err = dd->doc->SaveFile(as_cstring(args[1]));
    args[0] = val_bool(err == XML_SUCCESS);
    return 1;
}

/* XMLDoc.toString() → string */
static int xml_doc_to_string(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    DocData *dd = doc_data(args[0]);
    XMLPrinter printer;
    dd->doc->Print(&printer);
    ObjString *os = vm->make_string(printer.CStr(), printer.CStrSize() - 1);
    args[0] = val_obj((Obj *)os);
    return 1;
}

/* XMLDoc.root() → XMLElem or nil */
static int xml_doc_root(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    DocData *dd = doc_data(args[0]);
    ObjInstance *self = as_instance(args[0]);
    XMLElement *root = dd->doc->RootElement();
    args[0] = wrap_elem(vm, root, self);
    return 1;
}

/* XMLDoc.createElement(name) → XMLElem */
static int xml_doc_create_element(VM *vm, Value *args, int nargs)
{
    DocData *dd = doc_data(args[0]);
    ObjInstance *self = as_instance(args[0]);
    if (nargs < 2 || !is_string(args[1]))
    {
        vm->runtime_error("XMLDoc.createElement() expects (name)");
        return -1;
    }
    XMLElement *elem = dd->doc->NewElement(as_cstring(args[1]));
    args[0] = wrap_elem(vm, elem, self);
    return 1;
}

/* XMLDoc.setRoot(elem) — inserts elem as root */
static int xml_doc_set_root(VM *vm, Value *args, int nargs)
{
    DocData *dd = doc_data(args[0]);
    if (nargs < 2 || !is_instance(args[1]))
    {
        vm->runtime_error("XMLDoc.setRoot() expects (XMLElem)");
        return -1;
    }
    ElemData *ed = elem_data(args[1]);
    if (!ed || !ed->elem)
    {
        vm->runtime_error("XMLDoc.setRoot(): invalid element");
        return -1;
    }
    dd->doc->InsertEndChild(ed->elem);
    args[0] = val_bool(true);
    return 1;
}

/* XMLDoc.error() → string */
static int xml_doc_error(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    DocData *dd = doc_data(args[0]);
    const char *msg = dd->doc->ErrorStr();
    ObjString *os = vm->make_string(msg ? msg : "");
    args[0] = val_obj((Obj *)os);
    return 1;
}

/* XMLDoc.clear() */
static int xml_doc_clear(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    doc_data(args[0])->doc->Clear();
    args[0] = val_bool(true);
    return 1;
}

/* =========================================================
** XMLElem class
** ========================================================= */

static void *xml_elem_ctor(VM *, int, Value *)
{
    return new ElemData();
}

static void xml_elem_dtor(VM *, void *p)
{
    delete (ElemData *)p;
}

/* XMLElem.name() → string */
static int xml_elem_name(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    ElemData *ed = elem_data(args[0]);
    const char *n = (ed && ed->elem) ? ed->elem->Name() : "";
    args[0] = val_obj((Obj *)vm->make_string(n ? n : ""));
    return 1;
}

/* XMLElem.text() → string */
static int xml_elem_text(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    ElemData *ed = elem_data(args[0]);
    const char *t = (ed && ed->elem) ? ed->elem->GetText() : nullptr;
    args[0] = val_obj((Obj *)vm->make_string(t ? t : ""));
    return 1;
}

/* XMLElem.setText(str) */
static int xml_elem_set_text(VM *vm, Value *args, int nargs)
{
    ElemData *ed = elem_data(args[0]);
    if (!ed || !ed->elem)
    {
        args[0] = val_bool(false);
        return 1;
    }
    if (nargs < 2)
    {
        vm->runtime_error("XMLElem.setText() expects (value)");
        return -1;
    }
    if (is_string(args[1]))
        ed->elem->SetText(as_cstring(args[1]));
    else if (is_int(args[1]))
        ed->elem->SetText((int)args[1].as.integer);
    else if (is_float(args[1]))
        ed->elem->SetText(args[1].as.number);
    else if (is_bool(args[1]))
        ed->elem->SetText(args[1].as.boolean);
    args[0] = val_bool(true);
    return 1;
}

/* XMLElem.attr(name) → string or nil */
static int xml_elem_attr(VM *vm, Value *args, int nargs)
{
    ElemData *ed = elem_data(args[0]);
    if (!ed || !ed->elem || nargs < 2 || !is_string(args[1]))
    {
        args[0] = val_nil();
        return 1;
    }
    const char *val = ed->elem->Attribute(as_cstring(args[1]));
    if (!val)
        args[0] = val_nil();
    else
        args[0] = val_obj((Obj *)vm->make_string(val));
    return 1;
}

/* XMLElem.setAttr(name, value) */
static int xml_elem_set_attr(VM *vm, Value *args, int nargs)
{
    ElemData *ed = elem_data(args[0]);
    if (!ed || !ed->elem || nargs < 3 || !is_string(args[1]))
    {
        args[0] = val_bool(false);
        return 1;
    }
    const char *name = as_cstring(args[1]);
    if (is_string(args[2]))
        ed->elem->SetAttribute(name, as_cstring(args[2]));
    else if (is_int(args[2]))
        ed->elem->SetAttribute(name, (int)args[2].as.integer);
    else if (is_float(args[2]))
        ed->elem->SetAttribute(name, args[2].as.number);
    else if (is_bool(args[2]))
        ed->elem->SetAttribute(name, args[2].as.boolean);
    else
        ed->elem->SetAttribute(name, "");
    args[0] = val_bool(true);
    return 1;
}

/* XMLElem.removeAttr(name) */
static int xml_elem_remove_attr(VM *vm, Value *args, int nargs)
{
    ElemData *ed = elem_data(args[0]);
    if (!ed || !ed->elem || nargs < 2 || !is_string(args[1]))
    {
        args[0] = val_bool(false);
        return 1;
    }
    ed->elem->DeleteAttribute(as_cstring(args[1]));
    args[0] = val_bool(true);
    return 1;
}

/* XMLElem.attrs() → map of all attributes */
static int xml_elem_attrs(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    ElemData *ed = elem_data(args[0]);
    ObjMap *map = new_map(&vm->get_gc());
    if (ed && ed->elem)
    {
        for (const XMLAttribute *a = ed->elem->FirstAttribute(); a; a = a->Next())
        {
            Value key = val_obj((Obj *)vm->make_string(a->Name()));
            Value val = val_obj((Obj *)vm->make_string(a->Value()));
            map_set(&vm->get_gc(), map, key, val);
        }
    }
    args[0] = val_obj((Obj *)map);
    return 1;
}

/* XMLElem.child(name?) → XMLElem or nil */
static int xml_elem_child(VM *vm, Value *args, int nargs)
{
    ElemData *ed = elem_data(args[0]);
    ObjInstance *doc_inst = ed ? ed->doc_inst : nullptr;
    if (!ed || !ed->elem) { args[0] = val_nil(); return 1; }
    const char *name = nullptr;
    if (nargs >= 2 && is_string(args[1]))
        name = as_cstring(args[1]);
    XMLElement *child = ed->elem->FirstChildElement(name);
    args[0] = wrap_elem(vm, child, doc_inst);
    return 1;
}

/* XMLElem.firstChild() → XMLElem or nil (alias for child()) */
static int xml_elem_first_child(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    ElemData *ed = elem_data(args[0]);
    ObjInstance *doc_inst = ed ? ed->doc_inst : nullptr;
    if (!ed || !ed->elem) { args[0] = val_nil(); return 1; }
    XMLElement *child = ed->elem->FirstChildElement();
    args[0] = wrap_elem(vm, child, doc_inst);
    return 1;
}

/* XMLElem.next(name?) → next sibling XMLElem or nil */
static int xml_elem_next(VM *vm, Value *args, int nargs)
{
    ElemData *ed = elem_data(args[0]);
    ObjInstance *doc_inst = ed ? ed->doc_inst : nullptr;
    if (!ed || !ed->elem) { args[0] = val_nil(); return 1; }
    const char *name = nullptr;
    if (nargs >= 2 && is_string(args[1]))
        name = as_cstring(args[1]);
    XMLElement *sib = ed->elem->NextSiblingElement(name);
    args[0] = wrap_elem(vm, sib, doc_inst);
    return 1;
}

/* XMLElem.parent() → XMLElem or nil */
static int xml_elem_parent(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    ElemData *ed = elem_data(args[0]);
    ObjInstance *doc_inst = ed ? ed->doc_inst : nullptr;
    if (!ed || !ed->elem) { args[0] = val_nil(); return 1; }
    XMLNode *par = ed->elem->Parent();
    XMLElement *par_elem = par ? par->ToElement() : nullptr;
    args[0] = wrap_elem(vm, par_elem, doc_inst);
    return 1;
}

/* XMLElem.append(child_elem) */
static int xml_elem_append(VM *vm, Value *args, int nargs)
{
    ElemData *ed = elem_data(args[0]);
    if (!ed || !ed->elem || nargs < 2 || !is_instance(args[1]))
    {
        vm->runtime_error("XMLElem.append() expects (XMLElem)");
        return -1;
    }
    ElemData *child_ed = elem_data(args[1]);
    if (!child_ed || !child_ed->elem)
    {
        vm->runtime_error("XMLElem.append(): invalid child element");
        return -1;
    }
    ed->elem->InsertEndChild(child_ed->elem);
    args[0] = val_bool(true);
    return 1;
}

/* XMLElem.prepend(child_elem) */
static int xml_elem_prepend(VM *vm, Value *args, int nargs)
{
    ElemData *ed = elem_data(args[0]);
    if (!ed || !ed->elem || nargs < 2 || !is_instance(args[1]))
    {
        vm->runtime_error("XMLElem.prepend() expects (XMLElem)");
        return -1;
    }
    ElemData *child_ed = elem_data(args[1]);
    if (!child_ed || !child_ed->elem)
    {
        vm->runtime_error("XMLElem.prepend(): invalid child element");
        return -1;
    }
    ed->elem->InsertFirstChild(child_ed->elem);
    args[0] = val_bool(true);
    return 1;
}

/* XMLElem.remove() — detach from parent */
static int xml_elem_remove(VM *vm, Value *args, int nargs)
{
    (void)vm; (void)nargs;
    ElemData *ed = elem_data(args[0]);
    if (ed && ed->elem)
        ed->elem->DeleteChildren();
    args[0] = val_bool(true);
    return 1;
}

/* XMLElem.children() → array of XMLElem */
static int xml_elem_children(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    ElemData *ed = elem_data(args[0]);
    ObjInstance *doc_inst = ed ? ed->doc_inst : nullptr;
    ObjArray *arr = new_array(&vm->get_gc());
    if (ed && ed->elem)
    {
        for (XMLElement *ch = ed->elem->FirstChildElement(); ch; ch = ch->NextSiblingElement())
        {
            Value v = wrap_elem(vm, ch, doc_inst);
            array_push(&vm->get_gc(), arr, v);
        }
    }
    args[0] = val_obj((Obj *)arr);
    return 1;
}

/* XMLElem.toString() → XML string for this subtree */
static int xml_elem_to_string(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    ElemData *ed = elem_data(args[0]);
    if (!ed || !ed->elem) { args[0] = val_obj((Obj *)vm->make_string("")); return 1; }
    XMLPrinter printer;
    ed->elem->Accept(&printer);
    ObjString *os = vm->make_string(printer.CStr(), printer.CStrSize() - 1);
    args[0] = val_obj((Obj *)os);
    return 1;
}

/* =========================================================
** Init — register classes
** ========================================================= */

static void xml_elem_init(VM *vm)
{
    g_elem_class = vm->def_class("XMLElem")
        .ctor(xml_elem_ctor)
        .dtor(xml_elem_dtor)
        .method("name",       xml_elem_name,       0)
        .method("text",       xml_elem_text,        0)
        .method("setText",    xml_elem_set_text,    1)
        .method("attr",       xml_elem_attr,        1)
        .method("setAttr",    xml_elem_set_attr,    2)
        .method("removeAttr", xml_elem_remove_attr, 1)
        .method("attrs",      xml_elem_attrs,       0)
        .method("child",      xml_elem_child,      -1)
        .method("firstChild", xml_elem_first_child, 0)
        .method("next",       xml_elem_next,       -1)
        .method("parent",     xml_elem_parent,      0)
        .method("append",     xml_elem_append,      1)
        .method("prepend",    xml_elem_prepend,     1)
        .method("remove",     xml_elem_remove,      0)
        .method("children",   xml_elem_children,    0)
        .method("toString",   xml_elem_to_string,   0)
        .end();
}

static void xml_init(VM *vm)
{
    xml_elem_init(vm);

    vm->def_class("XMLDoc")
        .ctor(xml_doc_ctor)
        .dtor(xml_doc_dtor)
        .method("parse",         xml_doc_parse,          1)
        .method("loadFile",      xml_doc_load_file,       1)
        .method("saveFile",      xml_doc_save_file,       1)
        .method("toString",      xml_doc_to_string,       0)
        .method("root",          xml_doc_root,            0)
        .method("createElement", xml_doc_create_element,  1)
        .method("setRoot",       xml_doc_set_root,        1)
        .method("error",         xml_doc_error,           0)
        .method("clear",         xml_doc_clear,           0)
        .end();
}

/* =========================================================
** Module-level functions
** ========================================================= */

/* xml.parse(str) → XMLDoc */
static int nat_xml_parse(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("xml.parse() expects (xmlString)");
        return -1;
    }
    int db_gidx = vm->find_global("XMLDoc");
    if (db_gidx < 0) { vm->runtime_error("xml.parse(): XMLDoc not registered"); return -1; }
    Value cls_val = vm->get_global(db_gidx);
    if (!is_class(cls_val)) { vm->runtime_error("xml.parse(): XMLDoc not a class"); return -1; }
    Value inst = vm->make_instance(as_class(cls_val));
    if (is_nil(inst)) { vm->runtime_error("xml.parse(): failed to create XMLDoc"); return -1; }

    DocData *dd = doc_data(inst);
    dd->doc->Parse(as_cstring(args[0]));
    args[0] = inst;
    return 1;
}

/* xml.load(path) → XMLDoc */
static int nat_xml_load(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("xml.load() expects (path)");
        return -1;
    }
    int db_gidx = vm->find_global("XMLDoc");
    if (db_gidx < 0) { vm->runtime_error("xml.load(): XMLDoc not registered"); return -1; }
    Value cls_val = vm->get_global(db_gidx);
    if (!is_class(cls_val)) { vm->runtime_error("xml.load(): XMLDoc not a class"); return -1; }
    Value inst = vm->make_instance(as_class(cls_val));
    if (is_nil(inst)) { vm->runtime_error("xml.load(): failed to create XMLDoc"); return -1; }

    DocData *dd = doc_data(inst);
    dd->doc->LoadFile(as_cstring(args[0]));
    args[0] = inst;
    return 1;
}

/* xml.create() → empty XMLDoc */
static int nat_xml_create(VM *vm, Value *args, int nargs)
{
    (void)nargs;
    int db_gidx = vm->find_global("XMLDoc");
    if (db_gidx < 0) { vm->runtime_error("xml.create(): XMLDoc not registered"); return -1; }
    Value cls_val = vm->get_global(db_gidx);
    if (!is_class(cls_val)) { vm->runtime_error("xml.create(): XMLDoc not a class"); return -1; }
    Value inst = vm->make_instance(as_class(cls_val));
    if (is_nil(inst)) { vm->runtime_error("xml.create(): failed to create XMLDoc"); return -1; }
    args[0] = inst;
    return 1;
}

/* =========================================================
** Registration
** ========================================================= */

static const NativeReg xml_functions[] = {
    {"parse",  nat_xml_parse,  1},
    {"load",   nat_xml_load,   1},
    {"create", nat_xml_create, 0},
};

extern const NativeLib zen_lib_xml = {
    "xml",
    xml_functions,
    3,
    nullptr,
    0,
    xml_init
};

} /* namespace zen */
