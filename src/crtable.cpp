// Copyright (C) 2019 Greg Dionne
// Distributed under MIT License
#include "crtable.hpp"

std::string Reference::to_string(void)
{
  return expression.to_string();
}

bool CRTable::addlabel(const char *modulename, const char *labelname, int location, char *filename, int linenum)
{
  Label lbl(modulename, labelname, filename, linenum); 
  lbl.expression = Expression(location);
  return addlabel(lbl);
}

bool CRTable::addlabel(Label lbl)
{
  for (std::size_t i=0; i<labels.size(); ++i)
    if (labels[i].name == lbl.name) {
      fprintf(stderr,"Duplicate definition of label \"%s\"\n",lbl.name.c_str());
      return false;
    }
  
  labels.push_back(lbl);
  return true;
}

void CRTable::reportUnusedReferences(void)
{
   for (std::size_t i=0; i<labels.size(); ++i)
      if (!labels[i].used)
         fprintf(stderr,"%s:%i: warning: unused label \'%s\' [-Wunused]\n",
                 labels[i].fileName,labels[i].lineNumber,labels[i].name.c_str());
}

void CRTable::addreference(Reference r)
{
   references.push_back(r);
}

bool CRTable::resolve(Reference& r, int& result, std::string& offender)
{
   return r.expression.evaluate(labels, offender, result);
}

int CRTable::immediatelyResolve(int reftype, Fetcher& fetcher, const char *modulename, int pc, const char *dir, char *filename, int linenum)
{
   Reference r(pc, reftype, filename, linenum);
   r.expression.parse(fetcher, modulename, pc);

   int result;
   std::string offender;
   if (!resolve(r, result, offender))
      fetcher.die("label \"%s\" must be immediately resolveable when using \"%s\" or its equivalent pseudo-operation",
                  offender.c_str(),dir);

   return result;
}

int CRTable::tentativelyResolve(int reftype, Fetcher& fetcher, const char *modulename, int pc, char *filename, int linenum) 
{
   Reference r(pc, reftype, filename, linenum);
   r.expression.parse(fetcher, modulename, pc);

   int w = tentativelyResolve(r);
   if (reftype == 0 && (w < -128 || w > 127))
      fetcher.die("branch destination $%04X out of reach from $%04X",w+pc,pc);

   return w;
}

int CRTable::tentativelyResolve(Reference& r)
{
   int result;
   std::string offender;
   if (resolve(r, result, offender)) {
      if (r.reftype == 0) {
         result -= r.location + 2;
      }
      return result;
   } 

   addreference(r);
   return r.reftype==2 ? 0xdead : 0;
}

bool CRTable::resolveReferences(int startpc, unsigned char *binary, int& failpc) {
   for (std::size_t i=0; i<references.size(); i++) {
      Reference &r = references[i];
      int result;
      std::string offender;
      std::string refstr = r.to_string();

      if (!resolve(r, result, offender)) {
         fprintf(stderr,"%s:%i: error: ",r.filename,r.lineNumber);
         fprintf(stderr,"label \"%s\" is unresolved at %04x\n",offender.c_str(),r.location);
         failpc = r.location;
         return false;
      }

      if (r.reftype == 2) {
        if (result < -32768 || result > 65535) {
           fprintf(stderr,"%s:%i: error: ",r.filename,r.lineNumber);
           fprintf(stderr,"two-byte operand \"%s\", evaluating to %i for instruction at %04x, is out of range [-32768,65535].\n",refstr.c_str(), result, r.location);
           failpc = r.location;
           return false;
        }
        binary[r.location - startpc + 1] = (result >> 8) & 0xff;
        binary[r.location - startpc + 2] = result & 0xff;

      } else if (r.reftype == 1) {
        if (result < -128 || result > 255) {
           fprintf(stderr,"%s:%i: error: ",r.filename,r.lineNumber);
           fprintf(stderr,"single-byte operand \"%s\", evaluating to %i for instruction at %04x, is out of range [-128,255].\n",refstr.c_str(), result, r.location);
           failpc = r.location;
           return false;
        }
        binary[r.location - startpc + 1] = result & 0xff;

      } else if (r.reftype == 0) {
        result -= r.location + 2;
        if (result < -128 || result > 127) {
           fprintf(stderr,"%s:%i: error: ",r.filename,r.lineNumber);
           fprintf(stderr,"branch destination \"%s\", evaluating to %4x at %04x, is out of range\n",refstr.c_str(), result + r.location + 2, r.location);
           failpc = r.location;
           return false;
        }
        binary[r.location - startpc + 1] = result & 0xff;

      } else if (r.reftype == -1) {
        if (result < -128 || result > 255) {
           fprintf(stderr,"%s:%i: error: ",r.filename,r.lineNumber);
           fprintf(stderr,".byte reference \"%s\", evaluating to %i at %04x, is out of range.\n",refstr.c_str(), result, r.location);
           failpc = r.location;
           return false;
        }
        binary[r.location - startpc] = result & 0xff;

      } else if (r.reftype == -2) {
        if (result < -32768 || result > 65535) {
           fprintf(stderr,"%s:%i: error: ",r.filename,r.lineNumber);
           fprintf(stderr,".word reference \"%s\", evaluating to %i at %04x, is out of range.\n",refstr.c_str(), result, r.location);
           failpc = r.location;
           return false;
        }
        binary[r.location - startpc] = (result >> 8) & 0xff;
        binary[r.location - startpc + 1] = result & 0xff;
      }
   }

   return true;
}


