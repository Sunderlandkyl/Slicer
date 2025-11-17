/*==============================================================================

  Program: 3D Slicer

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

#ifndef __vtkMRMLMarkupsLabelDisplayNode_h
#define __vtkMRMLMarkupsLabelDisplayNode_h

#include "vtkMRMLLabelDisplayNode.h"

class vtkMRMLMarkupsNode;

/// \brief Label display node that exposes labels for markups control points.
class VTK_MRML_EXPORT vtkMRMLMarkupsLabelDisplayNode : public vtkMRMLLabelDisplayNode
{
public:
  static vtkMRMLMarkupsLabelDisplayNode* New();
  vtkTypeMacro(vtkMRMLMarkupsLabelDisplayNode, vtkMRMLLabelDisplayNode);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkMRMLNode* CreateNodeInstance() override;

  const char* GetNodeTagName() override { return "MarkupsLabelDisplay"; }

  // Multi-label API
  int GetNumberOfLabels() override;
  bool GetLabelInfo(int labelIndex, LabelInfo& info) override;

  // Observe target markups node changes
  vtkIntArray* GetNodeReferenceEvents(const char* referenceRole);
  void ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData) override;

protected:
  vtkMRMLMarkupsLabelDisplayNode();
  ~vtkMRMLMarkupsLabelDisplayNode() override;
  vtkMRMLMarkupsLabelDisplayNode(const vtkMRMLMarkupsLabelDisplayNode&);
  void operator=(const vtkMRMLMarkupsLabelDisplayNode&);

  vtkSmartPointer<vtkIntArray> TargetEvents;
};

#endif
