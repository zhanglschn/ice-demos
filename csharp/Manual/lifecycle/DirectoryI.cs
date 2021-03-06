// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// **********************************************************************

using System;
using System.Collections.Generic;
using Ice;
using Filesystem;

namespace FilesystemI
{
    public class DirectoryI : DirectoryDisp_, NodeI, DirectoryOperations_
    {
        // Slice name() operation.

        public override string name(Current c)
        {
            lock(this)
            {
                if(_destroyed)
                {
                    throw new ObjectNotExistException();
                }
                return _name;
            }
        }

        // Return the object identity for this node.

        public virtual Identity id()
        {
            return _id;
        }

        // Slice list() operation.

        public override NodeDesc[] list(Current c)
        {
            lock(this)
            {
                if(_destroyed)
                {
                    throw new ObjectNotExistException();
                }

                var ret = new NodeDesc[_contents.Count];
                int i = 0;
                foreach(var e in _contents)
                {
                    var p = e.Value;
                    ret[i] = new NodeDesc();
                    ret[i].name = e.Key;
                    ret[i].type = p is FileI ? NodeType.FileType : NodeType.DirType;
                    ret[i].proxy = NodePrxHelper.uncheckedCast(c.adapter.createProxy(p.id()));
                    ++i;
                }
                return ret;
            }
        }

        // Slice find() operation.

        public override NodeDesc find(string name, Current c)
        {
            lock(this)
            {
                if(_destroyed)
                {
                    throw new ObjectNotExistException();
                }


                NodeI p;
                if(!_contents.TryGetValue(name, out p))
                {
                    throw new NoSuchName(name);
                }

                var d = new NodeDesc();
                d.name = name;
                d.type = p is FileI ? NodeType.FileType : NodeType.DirType;
                d.proxy = NodePrxHelper.uncheckedCast(c.adapter.createProxy(p.id()));
                return d;
            }
        }

        // Slice createFile() operation.

        public override FilePrx createFile(string name, Current c)
        {
            lock(this)
            {
                if(_destroyed)
                {
                    throw new ObjectNotExistException();
                }

                if(name.Length == 0 || _contents.ContainsKey(name))
                {
                    throw new NameInUse(name);
                }

                var f = new FileI(name, this);
                var node = c.adapter.add(f, f.id());
                _contents.Add(name, f);
                return FilePrxHelper.uncheckedCast(node);
            }
        }

        // Slice createDirectory() operation.

        public override DirectoryPrx createDirectory(string name, Current c)
        {
            lock(this)
            {
                if(_destroyed)
                {
                    throw new ObjectNotExistException();
                }

                if(name.Length == 0 || _contents.ContainsKey(name))
                {
                    throw new NameInUse(name);
                }

                var d = new DirectoryI(name, this);
                var node = c.adapter.add(d, d.id());
                _contents.Add(name, d);
                return DirectoryPrxHelper.uncheckedCast(node);
            }
        }

        // Slice destroy() operation.

        public override void destroy(Current c)
        {
            if(_parent == null)
            {
                throw new PermissionDenied("Cannot destroy root directory");
            }

            lock(this)
            {
                if(_destroyed)
                {
                    throw new ObjectNotExistException();
                }

                if(_contents.Count != 0)
                {
                    throw new PermissionDenied("Cannot destroy non-empty directory");
                }

                c.adapter.remove(id());
                _destroyed = true;
            }

            _parent.removeEntry(_name);
        }

        // DirectoryI constructor for root directory.

        public DirectoryI()
            : this("/", null)
        {
        }

        // DirectoryI constructor. parent == null indicates root directory.

        public DirectoryI(string name, DirectoryI parent)
        {
            _name = name;
            _parent = parent;
            _id = new Identity();
            _destroyed = false;
            _contents = new Dictionary<string, NodeI>();

            if(parent == null)
            {
                _id.name = "RootDir";
            }
            else
            {
                _id.name = Guid.NewGuid().ToString();
            }
        }

        // Remove the entry from the _contents map.

        public void removeEntry(string name)
        {
            _contents.Remove(name);
        }

        private string _name; // Immutable
        private DirectoryI _parent; // Immutable
        private Identity _id; // Immutable
        private bool _destroyed;
        private Dictionary<string, NodeI> _contents;
    }
}
