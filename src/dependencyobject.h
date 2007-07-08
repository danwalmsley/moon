/*
 * dependencyobject.h: 
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */

#ifndef __MONO_DEPOBJECT_H__
#define __MONO_DEPOBJECT_H__

#include "value.h"
#include "enums.h"

class EventObject;

typedef void (*EventHandler) (EventObject *sender, gpointer calldata, gpointer closure);

//
// This guy provide reference counting
//
#define BASE_FLOATS 0x80000000

class Base {
 public:	
	guint32 refcount;
	Base () : refcount(BASE_FLOATS) { }
	virtual ~Base () { }
	
	void ref ();
	void unref ();
};

class EventObject : public Base {
 public:
	EventObject ();
	~EventObject ();
	
	void AddHandler (char *event_name, EventHandler handler, gpointer data);
	void RemoveHandler (char *event_name, EventHandler handler, gpointer data);

	void AddHandler (int event_id, EventHandler handler, gpointer data);
	void RemoveHandler (int event_id, EventHandler handler, gpointer data);

 protected:
	int  RegisterEvent (char *event_name);
	void Emit (char *event_name, gpointer calldata = NULL);
	void Emit (int event_id, gpointer calldata = NULL);

 private:
	GHashTable *event_name_hash;
	GPtrArray *events;
};


class DependencyObject : public EventObject {
 public:

	DependencyObject ();
	virtual ~DependencyObject ();
	static DependencyProperty *Register (Type::Kind type, const char *name, Value *default_value);
	static DependencyProperty *Register (Type::Kind type, const char *name, Type::Kind vtype);
	static DependencyProperty *Register (Type::Kind type, const char *name, Value *default_value, Type::Kind vtype);
	static DependencyProperty *RegisterNullable (Type::Kind type, const char *name, Type::Kind vtype);
	static DependencyProperty *RegisterFull (Type::Kind type, const char *name, Value *default_value, Type::Kind vtype, bool attached);
	
	static DependencyProperty *GetDependencyProperty (Type::Kind type, const char *name);
	static DependencyProperty *GetDependencyProperty (Type::Kind type, const char *name, bool inherits);
	static DependencyProperty *NameProperty;
	virtual void SetValue (DependencyProperty *property, Value *value);
	void SetValue (DependencyProperty *property, Value value);
	void SetValue (const char *name, Value *value);
	void SetValue (const char *name, Value value);
	virtual Value *GetValue (DependencyProperty *property);
	Value *GetValueNoDefault (DependencyProperty *property);
	Value *GetValue (const char *name);
	bool HasProperty (const char *name, bool inherits);
	DependencyProperty *GetDependencyProperty (const char *name);
	DependencyObject *FindName (const char *name);
	NameScope *FindNameScope ();

	static GHashTable *properties;

	virtual void OnPropertyChanged (DependencyProperty *property);
	virtual void OnSubPropertyChanged (DependencyProperty *prop, DependencyProperty *subprop) { }

	//
	// OnChildPropertyChanged:
	//    This is raised on objects when a child of this object has had one of its
	//    properties changed.   This is used so that owning objects can monitor if
	//    one of the attached properties in a child must be acted upon
	//
	//    This code will go up in the ownership chain until this is handled, by 
	//    returning TRUE.
	//
	virtual bool OnChildPropertyChanged (DependencyProperty *prop, DependencyObject *child) { return FALSE; }


	//
	// OnCollectionPropertyChanged:
	//
	virtual void OnCollectionChanged (Collection *col, CollectionChangeType type, DependencyObject *obj, DependencyProperty *prop) { }

	virtual Type::Kind GetObjectType ();
	Type* GetType ()
	{
		return Type::Find (GetObjectType ());
	};
	char* GetTypeName ()
	{
		return Type::Find (GetObjectType ())->name;
	}

	const char* GetName ()
	{
		Value *v = GetValue (DependencyObject::NameProperty);
		return v ? (v->AsString () ? v->AsString() : "(null)") : "(null)";
	}
	
	void SetParent (DependencyObject *parent);
	DependencyObject* GetParent ();

	//
	// Is:
	//    Similar to C#'s is: it checks if this object is of this kind or 
	//    a derived class.
	
	bool Is(Type::Kind k) {
		return GetType ()->IsSubclassOf (k);
	};

	static void Shutdown ();

	void Attach (DependencyProperty *property, DependencyObject *container);
	void Detach (DependencyProperty *property, DependencyObject *container);
	
 protected:
	void NotifyAttacheesOfPropertyChange (DependencyProperty *property);
	void NotifyParentOfPropertyChange (DependencyProperty *property, bool only_exact_type);

 private:
	GHashTable        *current_values;
	GSList            *attached_list;
	DependencyObject  *parent;
};


//
// DependencyProperty
//
class DependencyProperty {
 public:
	DependencyProperty () {} ;
	~DependencyProperty ();
	DependencyProperty (Type::Kind type, const char *name, Value *default_value, Type::Kind value_type, bool attached);

	char *name;
	Value *default_value;
	Type::Kind type;
	bool is_attached_property;
	Type::Kind value_type;
	bool is_nullable;
	bool IsNullable () { return is_nullable; }
};

G_BEGIN_DECLS

void base_ref (Base *base);
void base_unref (Base *base);


Value *dependency_object_get_value (DependencyObject *object, DependencyProperty *prop);
Value *dependency_object_get_value_no_default (DependencyObject *object, DependencyProperty *prop);
void   dependency_object_set_value (DependencyObject *object, DependencyProperty *prop, Value *val);

DependencyObject *dependency_object_find_name (DependencyObject *obj, const char *name, Type::Kind *element_type);
const char       *dependency_object_get_name  (DependencyObject *obj);

Type::Kind dependency_object_get_object_type (DependencyObject *obj);
const char *dependency_object_get_type_name (DependencyObject *obj);

void dependency_object_add_event_handler (DependencyObject *o, char *event, EventHandler handler, gpointer closure);
void dependency_object_remove_event_handler (DependencyObject *o, char *event, EventHandler handler, gpointer closure);



DependencyProperty *dependency_property_lookup (Type::Kind type, char *name);
char *dependency_property_get_name (DependencyProperty* property);
bool  dependency_property_is_nullable (DependencyProperty* property);
Type::Kind dependency_property_get_value_type (DependencyProperty* property);
DependencyProperty *resolve_property_path (DependencyObject **o, const char *path);

void dependencyobject_init (void);

G_END_DECLS

#endif /* __MONO_DEPOBJECT_H__ */

