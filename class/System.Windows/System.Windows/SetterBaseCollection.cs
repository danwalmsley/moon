//
// SetterBaseCollection.cs
//
// Contact:
//   Moonlight List (moonlight-list@lists.ximian.com)
//
// Copyright 2008 Novell, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

using Mono;

namespace System.Windows {

	public sealed class SetterBaseCollection : PresentationFrameworkCollection<SetterBase> {
		bool isSealed;

		public SetterBaseCollection () : base (NativeMethods.dependency_object_collection_new ())
		{
		}

		public bool IsSealed {
			get { return isSealed; }
		}

		internal void Seal ()
		{
			isSealed = true;
			foreach (SetterBase setter in this)
				setter.Seal ();
		}

		public override void Add (SetterBase value)
		{
			AddImpl (value);
		}
		
		public override bool Contains (SetterBase value)
		{
			return ContainsImpl (value);
		}
		
		public override int IndexOf (SetterBase value)
		{
			return IndexOfImpl (value);
		}
		
		public override void Insert (int index, SetterBase value)
		{
			InsertImpl (index, value);
		}
		
		public override bool Remove (SetterBase value)
		{
			return RemoveImpl (value);
		}
		
		public override SetterBase this[int index] {
			get { return GetItemImpl (index); }
			set { SetItemImpl (index, value); }
		}
	}

}