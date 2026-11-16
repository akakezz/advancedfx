"""Check classic XML against the unmodified HLAE loader and interpolator.
Only compiler portability adapters are applied to the historical sources.
"""
import argparse, math, subprocess, tempfile, xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('xml',type=Path);p.add_argument('--ref',default='7c52c93f');a=p.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='hlae-stock-bake-') as tmp:
    work=Path(tmp);(work/'shared').mkdir();(work/'deps').symlink_to(root/'deps',target_is_directory=True)
    for name in ['CamPath.cpp','CamPath.h','AfxMath.cpp','AfxMath.h']:
        text=subprocess.check_output(['git','show',f'{a.ref}:shared/{name}'],cwd=root).decode()
        text=text.replace('const_iterator &','const_iterator ').replace('const_iterator&','const_iterator ')
        text=text.replace('std::ofstream ofs(fileName,','std::ofstream ofs(std::filesystem::path(fileName),')
        (work/'shared'/name).write_text(text)
    (work/'shared/stdafx.h').write_text('#include <cstring>\n#include <string>\n#include <cstdlib>\n#include <filesystem>\n')
    (work/'check.cpp').write_text(r'''
#include "shared/CamPath.h"
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){CamPath path;if(argc!=2||!path.Load(std::filesystem::path(argv[1]).wstring().c_str())||!path.CanEval())return 1;
std::cout.precision(17);auto emit=[&](double t){auto v=path.Eval(t);std::cout<<t<<" "<<v.X<<" "<<v.Y<<" "<<v.Z<<" "<<v.Fov<<" "<<v.R.W<<" "<<v.R.X<<" "<<v.R.Y<<" "<<v.R.Z<<"\n";};
for(auto i=path.GetBegin();i!=path.GetEnd();++i){emit(i.GetTime());auto n=i;++n;if(n!=path.GetEnd())emit((i.GetTime()+n.GetTime())*.5);}}
''')
    subprocess.run(['g++','-std=c++17','-O0','-Dabstract=','-include',str(root/'tests/cinematic/msvc_crt_compat.h'),str(work/'check.cpp'),str(work/'shared/CamPath.cpp'),str(work/'shared/AfxMath.cpp'),'-o',str(work/'check')],check=True,capture_output=True)
    lines=subprocess.check_output([str(work/'check'),str(a.xml.resolve())],text=True).splitlines()
    points=[{k:float(v) for k,v in e.attrib.items()} for e in ET.parse(a.xml).findall('./points/p')]
    assert len(lines)==2*len(points)-1
    def quat(e):return [e[k] for k in ['qw','qx','qy','qz']]
    for index,line in enumerate(lines):
        values=list(map(float,line.split()));left=points[index//2];right=points[min(index//2+index%2,len(points)-1)];u=.5 if index%2 else 0
        for channel,key in enumerate(['t','x','y','z','fov']):
            expected=left[key]+u*(right[key]-left[key]);assert abs(values[channel]-expected)<1e-6,(key,index,values[channel],expected)
        q,r=quat(left),quat(right);dot=sum(x*y for x,y in zip(q,r))
        if dot<0:r=[-x for x in r];dot=-dot
        if dot>.9995:expected=[x+u*(y-x) for x,y in zip(q,r)]
        else:
            angle=math.acos(min(1,dot));expected=[(math.sin((1-u)*angle)*x+math.sin(u*angle)*y)/math.sin(angle) for x,y in zip(q,r)]
        actual=values[5:];dot=abs(sum(x*y for x,y in zip(actual,expected)))/math.sqrt(sum(x*x for x in actual)*sum(x*x for x in expected));assert 1-dot<1e-8,index
    print(f'Stock HLAE {a.ref}: {len(points)} keys and every interval midpoint match classic XML.')
