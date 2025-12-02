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

#ifndef __vtkMRMLSegmentationLabelDisplayNode_h
#define __vtkMRMLSegmentationLabelDisplayNode_h

#include "vtkMRMLLabelDisplayNode.h"

class vtkMRMLSegmentationNode;
class vtkMRMLSegmentationDisplayNode;

/// \brief Label display node that exposes labels for all visible segments in a segmentation.
class VTK_MRML_EXPORT vtkMRMLSegmentationLabelDisplayNode : public vtkMRMLLabelDisplayNode
{
public:
  static vtkMRMLSegmentationLabelDisplayNode* New();
  vtkTypeMacro(vtkMRMLSegmentationLabelDisplayNode, vtkMRMLLabelDisplayNode);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkMRMLNode* CreateNodeInstance() override;

  const char* GetNodeTagName() override { return "SegmentationLabelDisplay"; }

  // Multi-label API
  int GetNumberOfLabels() override;
  bool GetLabelInfo(int labelIndex, LabelInfo& info) override;

  // Observe target segmentation node changes
  vtkIntArray* GetNodeReferenceEvents(const char* referenceRole);
  void ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData) override;

protected:
  vtkMRMLSegmentationLabelDisplayNode();
  ~vtkMRMLSegmentationLabelDisplayNode() override;
  vtkMRMLSegmentationLabelDisplayNode(const vtkMRMLSegmentationLabelDisplayNode&);
  void operator=(const vtkMRMLSegmentationLabelDisplayNode&);

protected:
  // Cache of current visible segment IDs; rebuilt on each query
  std::vector<std::string> GetSegmentIDs();

  // Invalidate the segment center cache when segmentation changes
  void InvalidateSegmentCenterCache();

  // Get cached segment center (calculates and caches if needed)
  bool GetCachedSegmentCenter(const std::string& segmentID, double center[3]);

  vtkSmartPointer<vtkIntArray> TargetEvents;

  // Cache for segment centers to avoid expensive recalculation
  std::map<std::string, std::array<double, 3>> SegmentCenterCache;
};

#endif
